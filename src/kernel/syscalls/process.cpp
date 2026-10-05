/**
 * 进程管理相关系统调用实现
 * 
 * exit / fork / exec / waitpid / getpid / getppid / yield / nanosleep / kill
 */

#include <kernel/syscalls/process.h>
#include <kernel/task.h>
#include <kernel/elf.h>
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <kernel/gdt.h>
#endif
#include <kernel/interrupt.h>
#include <hal/hal.h>
#include <hal/pgtable.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/klog.h>
#include <lib/string.h>

/* 默认时间片（与 task.c 保持一致） */
#define DEFAULT_TIME_SLICE 10

/* exec 接受的 ELF 映像大小上限 */
#define EXEC_MAX_IMAGE_SIZE (16u * 1024 * 1024)

/**
 * syscall::Process::exit - 退出当前进程
 */
void syscall::Process::exit(uint32_t code) {
    LOG_DEBUG_MSG("syscall::Process::exit: exit_code=%u\n", code);
    
    task_t *current = kernel::Scheduler::get_current();
    if (current) {
        current->exit_code = code;
        LOG_DEBUG_MSG("syscall::Process::exit: process %u (%s) exiting with code %u\n", 
                      current->pid, current->name, code);
    }
    
    // 调用任务管理器的退出函数
    task_exit(code);
    
    // 永远不会执行到这里
    while (1) {
        hal::Cpu::halt();
    }
}

/**
 * syscall::Process::fork - 创建子进程（简化包装器）
 * 
 * 注意：这个函数实际上不会被直接调用
 * 系统调用包装器会调用 sys_fork_with_frame
 */
uint32_t syscall::Process::fork(uintptr_t *frame) {
    // 禁用中断，保证 fork 过程的原子性
    bool prev_state = kernel::Interrupts::disable();
    
    task_t *parent = kernel::Scheduler::get_current();
    if (!parent) {
        LOG_ERROR_MSG("syscall::Process::fork: No current task\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("syscall::Process::fork: Parent PID %u\n", parent->pid);
    
    // 只有用户进程才能 fork
    if (!parent->is_user_process) {
        LOG_ERROR_MSG("syscall::Process::fork: Cannot fork kernel thread\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-1;
    }
    
    // 【内存安全检查】检查是否有足够内存创建新进程
    // 需要：1个页目录 + 页表（最多512个） + 其他开销
    // 注意：由于使用 COW，不需要预留用户栈的全部 2048 页
    // 但需要预留足够的页表和页目录
    mm::PmmInfo mem_info = mm::Pmm::get_info();
    uint32_t min_required_frames = 64;  // 页目录 + 页表 + 内核栈 + 其他
    if (mem_info.free_frames < min_required_frames) {
        LOG_ERROR_MSG("syscall::Process::fork: Insufficient memory (free=%llu, required>=%u)\n",
                     (unsigned long long)mem_info.free_frames, min_required_frames);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;  // ENOMEM
    }
    
    // 直接从传递的 frame 参数读取用户态寄存器
    // frame 指针由 syscall_handler 传递，指向它保存寄存器的位置
    // 注意：栈帧布局是架构相关的
#if defined(ARCH_ARM64)
    // ARM64 栈帧布局（vectors.S kernel_entry）：
    //   frame[0-30]  = X0-X30 (general purpose registers)
    //   frame[31]    = SP_EL0 (user stack pointer)
    //   frame[32]    = ELR_EL1 (user PC / return address)
    //   frame[33]    = SPSR_EL1 (user PSTATE)
    // Note: X8 contains syscall number, X0-X5 contain arguments
    uintptr_t user_sp     = frame[31];  // SP_EL0 (user stack pointer)
    uintptr_t user_pc     = frame[32];  // ELR_EL1 (user PC)
    uintptr_t user_pstate = frame[33];  // SPSR_EL1 (user PSTATE)
    
    LOG_DEBUG_MSG("syscall::Process::fork: Captured ARM64 user context:\n");
    LOG_DEBUG_MSG("  PC=0x%lx SP=0x%lx PSTATE=0x%lx\n", 
                  (unsigned long)user_pc, (unsigned long)user_sp, (unsigned long)user_pstate);
    LOG_DEBUG_MSG("  X0=0x%lx X1=0x%lx X8=0x%lx X30=0x%lx\n",
                  (unsigned long)frame[0], (unsigned long)frame[1], 
                  (unsigned long)frame[8], (unsigned long)frame[30]);
#elif defined(ARCH_X86_64)
    // x86_64 栈帧布局（syscall64_asm.asm）：
    //   frame[0]  = r15
    //   frame[1]  = r14
    //   ...
    //   frame[14] = rax (syscall number)
    //   frame[15] = user_rsp
    uintptr_t user_ds     = 0x23;       // 用户数据段（x86_64 不使用 DS）
    uintptr_t user_eax    = frame[14];  // RAX (系统调用号)
    uintptr_t user_ebx    = frame[13];  // RBX
    uintptr_t user_ecx    = frame[12];  // RCX (user RIP)
    uintptr_t user_edx    = frame[11];  // RDX
    uintptr_t user_esi    = frame[10];  // RSI
    uintptr_t user_edi    = frame[9];   // RDI
    uintptr_t user_ebp    = frame[8];   // RBP
    uintptr_t user_eip    = frame[12];  // RCX = user RIP (SYSCALL saves RIP to RCX)
    uintptr_t user_cs     = 0x1B;       // 用户代码段
    uintptr_t user_eflags = frame[4];   // R11 = user RFLAGS
    uintptr_t user_esp    = frame[15];  // user RSP
    uintptr_t user_ss     = 0x23;       // 用户栈段
    
    (void)user_eax;  // 系统调用号，不需要复制
    
    LOG_DEBUG_MSG("syscall::Process::fork: Captured user context:\n");
    LOG_DEBUG_MSG("  EIP=0x%lx ESP=0x%lx EBP=0x%lx\n", (unsigned long)user_eip, (unsigned long)user_esp, (unsigned long)user_ebp);
    LOG_DEBUG_MSG("  CS=0x%lx SS=0x%lx DS=0x%lx EFLAGS=0x%lx\n", 
                  (unsigned long)user_cs, (unsigned long)user_ss, (unsigned long)user_ds, (unsigned long)user_eflags);
#else
    // i686 栈帧布局（syscall_asm.asm）：
    uintptr_t user_ds     = frame[0];   // DS
    uintptr_t user_eax    = frame[1];   // EAX (系统调用号)
    uintptr_t user_ebx    = frame[2];   // EBX
    uintptr_t user_ecx    = frame[3];   // ECX
    uintptr_t user_edx    = frame[4];   // EDX
    uintptr_t user_esi    = frame[5];   // ESI
    uintptr_t user_edi    = frame[6];   // EDI
    uintptr_t user_ebp    = frame[7];   // EBP
    uintptr_t user_eip    = frame[8];   // EIP (IRET)
    uintptr_t user_cs     = frame[9];   // CS (IRET)
    uintptr_t user_eflags = frame[10];  // EFLAGS (IRET)
    uintptr_t user_esp    = frame[11];  // ESP (IRET)
    uintptr_t user_ss     = frame[12];  // SS (IRET)
    
    (void)user_eax;  // 系统调用号，不需要复制
    
    LOG_DEBUG_MSG("syscall::Process::fork: Captured user context:\n");
    LOG_DEBUG_MSG("  EIP=0x%lx ESP=0x%lx EBP=0x%lx\n", (unsigned long)user_eip, (unsigned long)user_esp, (unsigned long)user_ebp);
    LOG_DEBUG_MSG("  CS=0x%lx SS=0x%lx DS=0x%lx EFLAGS=0x%lx\n", 
                  (unsigned long)user_cs, (unsigned long)user_ss, (unsigned long)user_ds, (unsigned long)user_eflags);
#endif
    
#if !defined(ARCH_ARM64)
    // 【安全检查】验证父进程顶层页表的完整性（仅检查前几项）：存在的项必须指向
    // 一个由 PMM 管理的页帧。用架构自己的页表项格式和完整宽度的物理地址来判断
    // Note: This check is x86-specific (page directory structure)
    page_directory_t *parent_dir = parent->page_dir;
    const paddr_t phys_end = (paddr_t)mm::Pmm::get_info().total_frames * PAGE_SIZE;
    for (uint32_t i = 0; i < 10; i++) {
        if (pgtable_is_present(parent_dir->entries[i])) {
            paddr_t phys = pgtable_get_phys(parent_dir->entries[i]);
            if (phys == 0 || phys >= phys_end) {
                LOG_ERROR_MSG("syscall::Process::fork: Parent PDE[%u] corrupted: 0x%llx (phys=0x%llx)\n",
                             i, (unsigned long long)parent_dir->entries[i], (unsigned long long)phys);
                LOG_ERROR_MSG("  Parent: PID=%u, name=%s, page_dir=%p, page_dir_phys=0x%llx\n",
                             parent->pid, parent->name, parent_dir,
                             (unsigned long long)parent->page_dir_phys);
                // 打印更多 PDE 以帮助诊断
                LOG_ERROR_MSG("  PDE[0]=0x%llx, PDE[1]=0x%llx, PDE[2]=0x%llx, PDE[3]=0x%llx\n",
                             (unsigned long long)parent_dir->entries[0],
                             (unsigned long long)parent_dir->entries[1],
                             (unsigned long long)parent_dir->entries[2],
                             (unsigned long long)parent_dir->entries[3]);
                kernel::Interrupts::restore(prev_state);
                return (uint32_t)-1;
            }
        }
    }
#endif
    
    // 分配子进程 PCB
    task_t *child = kernel::Scheduler::alloc();
    if (!child) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to allocate PCB\n");
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    
    // 复制父进程信息
    strncpy(child->name, parent->name, sizeof(child->name) - 1);
    child->name[sizeof(child->name) - 1] = '\0';
    child->is_user_process = true;
    child->priority = parent->priority;
    child->time_slice = DEFAULT_TIME_SLICE;
    
    // 以下错误路径的约定：一旦资源记录到子进程 PCB（page_dir_phys、
    // kernel_stack_base），就只由 kernel::Scheduler::free(child) 释放。
    // 不要在调用它之前再手动释放一次——那会二次释放内核栈，并把与父进程
    // COW 共享的物理页的引用计数多减一次。

    // 克隆页目录（深拷贝，完全复制物理页）
    child->page_dir_phys = mm::Vmm::clone_page_directory(parent->page_dir_phys);
    if (!child->page_dir_phys) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to clone page directory\n");
        kernel::Scheduler::free(child);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    child->page_dir = (page_directory_t*)PHYS_TO_VIRT(child->page_dir_phys);
    
    // 分配内核栈
    child->kernel_stack_base = (uintptr_t)kmalloc(KERNEL_STACK_SIZE);
    if (!child->kernel_stack_base) {
        LOG_ERROR_MSG("syscall::Process::fork: Failed to allocate kernel stack\n");
        kernel::Scheduler::free(child);
        kernel::Interrupts::restore(prev_state);
        return (uint32_t)-12;
    }
    child->kernel_stack = child->kernel_stack_base + KERNEL_STACK_SIZE;
    
    // 复制用户空间信息（已在页目录中共享）
    child->user_stack_base = parent->user_stack_base;
    child->user_stack = parent->user_stack;
    child->user_entry = parent->user_entry;
    
    // 复制堆信息
    child->heap_start = parent->heap_start;
    child->heap_end = parent->heap_end;
    child->heap_max = parent->heap_max;
    
    // 初始化子进程上下文
    // 按照 Unix fork 语义：子进程从 fork() 调用返回处继续执行
    memset(&child->context, 0, sizeof(cpu_context_t));
    
#if defined(ARCH_ARM64)
    // ARM64: 复制父进程的用户态寄存器
    // 子进程 fork 返回 0（X0 = 0）
    child->context.x[0] = 0;  // 子进程 fork 返回 0
    // 复制其他寄存器（X1-X30）从保存的帧中
    for (int i = 1; i < 31; i++) {
        child->context.x[i] = frame[i];
    }
    child->context.sp = user_sp;      // 使用父进程当前的用户栈指针
    child->context.pc = user_pc;      // 从 fork() 调用返回处继续
    child->context.pstate = ARM64_PSTATE_EL0t;  // 用户模式，中断使能
    child->context.ttbr0 = child->page_dir_phys;
    child->context.kernel_sp = child->kernel_stack;  // 子进程自己的内核栈
    
    LOG_DEBUG_MSG("syscall::Process::fork: Child ARM64 context:\n");
    LOG_DEBUG_MSG("  PC=0x%llx SP=0x%llx PSTATE=0x%llx TTBR0=0x%llx\n",
                  (unsigned long long)child->context.pc, 
                  (unsigned long long)child->context.sp,
                  (unsigned long long)child->context.pstate,
                  (unsigned long long)child->context.ttbr0);
#else
    // x86: 复制父进程的所有用户态寄存器
    child->context.eax = 0;  // 子进程 fork 返回 0（唯一的区别）
    child->context.ebx = user_ebx;
    child->context.ecx = user_ecx;
    child->context.edx = user_edx;
    child->context.esi = user_esi;
    child->context.edi = user_edi;
    child->context.ebp = user_ebp;
    child->context.esp = user_esp;  // 使用父进程当前的用户栈指针
    child->context.eip = user_eip;  // 从 fork() 调用返回处继续
#if defined(ARCH_X86_64)
    // x86_64 还有 R8-R15。其中 R12-R15 是被调用者保存的寄存器，子进程从
    // fork() 返回后调用者保存在里面的值必须和父进程一致。
    // 帧布局（syscall64_asm.asm）：frame[0..7] = r15, r14, r13, r12, r11, r10, r9, r8
    child->context.r15 = frame[0];
    child->context.r14 = frame[1];
    child->context.r13 = frame[2];
    child->context.r12 = frame[3];
    child->context.r11 = frame[4];
    child->context.r10 = frame[5];
    child->context.r9  = frame[6];
    child->context.r8  = frame[7];
#endif

    // 清理 EFLAGS 中的敏感位，防止权限提升
    // 保留：CF, PF, AF, ZF, SF, OF, DF, IF
    // 清除：IOPL, NT, RF, VM, AC, VIF, VIP, ID
    child->context.eflags = (user_eflags & 0x00000CD5) | 0x00000202;  // IF=1
    
    child->context.cr3 = child->page_dir_phys;
    
    // 复制段寄存器（强制使用 Ring 3 段，防止权限提升）
    // 不信任用户提供的段选择子，强制设置为用户态段
#if defined(ARCH_X86_64)
    // x86_64 GDT layout:
    //   0x18 (index 3) = User Data → 0x1B with RPL=3
    //   0x20 (index 4) = User Code → 0x23 with RPL=3
    child->context.cs = 0x23;  // 用户代码段（Ring 3）
    child->context.ss = 0x1B;  // 用户数据段（Ring 3）
#else
    // i686 GDT layout:
    //   0x18 (index 3) = User Code → 0x1B with RPL=3
    //   0x20 (index 4) = User Data → 0x23 with RPL=3
    child->context.cs = 0x1B;  // 用户代码段（Ring 3）
    child->context.ss = 0x23;  // 用户栈段（Ring 3）
    // i686: 需要设置所有段寄存器
    child->context.ds = 0x23;  // 用户数据段（Ring 3）
    child->context.es = 0x23;
    child->context.fs = 0x23;
    child->context.gs = 0x23;
#endif
#endif /* ARCH_ARM64 */
    
    // 设置父子关系
    child->parent = parent;
    child->privileged = parent->privileged;  // fork 继承特权
    
    // 添加到就绪队列
    child->state = TASK_READY;
    kernel::Scheduler::ready_queue_add(child);
    
    LOG_DEBUG_MSG("syscall::Process::fork: Created child PID %u\n", child->pid);
    
    // 恢复中断状态
    kernel::Interrupts::restore(prev_state);
    
    // 父进程返回子进程 PID
    return child->pid;
}

/**
 * syscall::Process::exec - 用一个 ELF 映像替换当前进程
 *
 * 内核不认识文件：映像由调用者提供（位于调用者的用户地址空间，
 * 系统调用入口已校验可读）。
 *
 * @param frame 系统调用栈帧指针（架构相关大小）
 * @param image ELF 映像
 * @param size  映像大小
 * @param args  参数块（"arg0\0arg1\0..."），可为 NULL
 * @param args_size 参数块长度，不超过 USER_ARGS_MAX
 * @return 成功则不返回到原程序，失败返回 -1
 */
uint32_t syscall::Process::exec(uintptr_t *frame, const void *image, size_t size,
                                const char *args, size_t args_size) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current || !frame || !image) {
        return (uint32_t)-1;
    }

    if (size == 0 || size > EXEC_MAX_IMAGE_SIZE) {
        LOG_ERROR_MSG("syscall::Process::exec: bad image size %llu\n", (unsigned long long)size);
        return (uint32_t)-1;
    }
    uint32_t file_size = (uint32_t)size;

    // 复制到内核：加载时要切到新地址空间，旧地址空间里的映像就看不到了
    void *elf_data = kmalloc(file_size);
    if (!elf_data) {
        LOG_ERROR_MSG("syscall::Process::exec: out of memory for ELF image\n");
        return (uint32_t)-1;
    }
    memcpy(elf_data, image, file_size);

    // 参数块同样要先拿进内核；它总是以 NUL 结尾
    if (!args || args_size > USER_ARGS_MAX) {
        args_size = 0;
    }
    char *kargs = NULL;
    if (args_size > 0) {
        kargs = (char *)kmalloc(args_size);
        if (!kargs) {
            kfree(elf_data);
            return (uint32_t)-1;
        }
        memcpy(kargs, args, args_size);
        kargs[args_size - 1] = '\0';
    }

    // 完整校验 ELF 映像（文件头、程序头表、各段的文件范围和地址范围、入口点），
    // 在创建新地址空间之前就拒绝不合法的映像
    if (!kernel::Elf::validate(elf_data, file_size)) {
        LOG_DEBUG_MSG("syscall::Process::exec: invalid ELF image\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }

    uintptr_t entry_point = kernel::Elf::get_entry(elf_data, file_size);
    if (entry_point == 0) {
        LOG_ERROR_MSG("syscall::Process::exec: no entry point\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }

    // ============================================================================
    // 创建新的地址空间
    // 必须使用新的页目录，否则直接在旧页目录上加载会导致：
    // 1. 覆盖旧映射时泄露物理页
    // 2. 失败时无法回滚
    // ============================================================================
    
    uintptr_t new_dir_phys = mm::Vmm::create_page_directory();
    if (!new_dir_phys) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to create new page directory\n");
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }
#if defined(ARCH_ARM64)
    // ARM64: new_dir_phys is the address space handle (TTBR0 physical address)
    // We don't use page_directory_t* directly on ARM64
    page_directory_t *new_dir = (page_directory_t*)(uintptr_t)new_dir_phys;
#else
    page_directory_t *new_dir = (page_directory_t*)PHYS_TO_VIRT(new_dir_phys);
#endif
    
    // 保存旧的页目录信息，用于回滚或释放
    page_directory_t *old_dir = current->page_dir;
    uintptr_t old_dir_phys = current->page_dir_phys;
    
    // 加载 ELF 到新页目录
    uintptr_t program_end;
    if (!kernel::Elf::load(elf_data, file_size, new_dir, &entry_point, &program_end)) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to load ELF\n");
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(elf_data);
        kfree(kargs);
        return (uint32_t)-1;
    }
    
    // 释放 ELF 数据（已经加载到新页目录的物理页中了）
    kfree(elf_data);
    
    // 临时更新进程的页目录指针，以便 kernel::Scheduler::setup_user_stack 操作新目录
    current->page_dir = new_dir;
    current->page_dir_phys = new_dir_phys;
    
    // 【内存安全检查】在分配用户栈前检查是否有足够内存
    // USER_STACK_SIZE / PAGE_SIZE = 需要的页数，再加一些页表开销
    uint32_t stack_pages_needed = (USER_STACK_SIZE / PAGE_SIZE) + 4;  // +4 用于页表
    mm::PmmInfo execve_mem_info = mm::Pmm::get_info();
    if (execve_mem_info.free_frames < stack_pages_needed) {
        LOG_ERROR_MSG("syscall::Process::exec: Insufficient memory for user stack (free=%llu, required=%u)\n",
                     (unsigned long long)execve_mem_info.free_frames, stack_pages_needed);
        // 回滚
        current->page_dir = old_dir;
        current->page_dir_phys = old_dir_phys;
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(kargs);
        return (uint32_t)-1;  // ENOMEM
    }
    
    // 在新页目录中设置用户栈
    if (!kernel::Scheduler::setup_user_stack(current)) {
        LOG_ERROR_MSG("syscall::Process::exec: failed to setup user stack\n");
        // 回滚
        current->page_dir = old_dir;
        current->page_dir_phys = old_dir_phys;
        mm::Vmm::free_page_directory(new_dir_phys);
        kfree(kargs);
        return (uint32_t)-1;
    }
    
    // 设置堆管理
    // 堆从程序结束后的下一页开始
    current->heap_start = PAGE_ALIGN_UP(program_end);
    current->heap_end = current->heap_start;
    // 堆最大值：留出 8MB 给栈
    current->heap_max = current->user_stack_base - (8 * 1024 * 1024);
    
    LOG_DEBUG_MSG("syscall::Process::exec: heap: start=0x%llx, end=0x%llx, max=0x%llx\n", 
                 (unsigned long long)current->heap_start, 
                 (unsigned long long)current->heap_end, 
                 (unsigned long long)current->heap_max);
    
    // ============================================================================
    // 切换到新地址空间
    // ============================================================================
    
    mm::Vmm::switch_page_directory(new_dir_phys);

    // 现在处于新地址空间：把参数写进参数页（新栈的页都已清零）
    if (kargs) {
        // 进程以 argv[0] 命名
        strncpy(current->name, kargs, sizeof(current->name) - 1);
        current->name[sizeof(current->name) - 1] = '\0';

        user_args_t *uargs = (user_args_t *)USER_ARGS_ADDR;
        uargs->length = (uint32_t)args_size;
        memcpy(uargs->data, kargs, args_size);
        kfree(kargs);
    }
    
    // 释放旧页目录及其映射的所有用户空间物理页
    // 这解决了 exec 覆盖映射导致的内存泄露问题
    mm::Vmm::free_page_directory(old_dir_phys);
    
    // 更新进程信息
    current->user_entry = entry_point;
    current->is_user_process = true;
    
    // 设置用户态上下文
#if defined(ARCH_ARM64)
    // ARM64: 设置用户模式上下文
    current->context.pc = entry_point;
    current->context.sp = current->user_stack;
    current->context.pstate = ARM64_PSTATE_EL0t;  // 用户模式
    current->context.ttbr0 = current->page_dir_phys;
#else
    // x86: 设置用户态上下文
    current->context.eip = entry_point;
#if defined(ARCH_X86_64)
    // x86_64 GDT layout:
    //   0x18 (index 3) = User Data → 0x1B with RPL=3
    //   0x20 (index 4) = User Code → 0x23 with RPL=3
    current->context.cs = 0x23;  // 用户代码段（Ring 3）
    current->context.ss = 0x1B;  // 用户数据段（Ring 3）
#else
    // i686 GDT layout:
    //   0x18 (index 3) = User Code → 0x1B with RPL=3
    //   0x20 (index 4) = User Data → 0x23 with RPL=3
    current->context.cs = 0x1B;  // 用户代码段（Ring 3）
    current->context.ss = 0x23;  // 用户栈段（Ring 3）
    // i686: 需要设置所有段寄存器
    current->context.ds = 0x23;  // 用户数据段（Ring 3）
    current->context.es = 0x23;
    current->context.fs = 0x23;
    current->context.gs = 0x23;
#endif
    current->context.esp = current->user_stack;
    current->context.eflags = 0x202;  // 中断使能
    current->context.cr3 = current->page_dir_phys;
#endif /* ARCH_ARM64 */
    
    // ============================================================================
    // 关键修复：修改系统调用栈帧，让 iret 返回到新程序的入口点
    // ============================================================================
    // 
    // 系统调用栈帧布局（从 syscall_asm.asm）：
    //   frame[0] = DS
    //   frame[1] = EAX (返回值)
    //   frame[2] = EBX
    //   frame[3] = ECX
    //   frame[4] = EDX
    //   frame[5] = ESI
    //   frame[6] = EDI
    //   frame[7] = EBP
    //   
    // 在 frame 之后（更高地址），CPU 自动压入的 IRET 栈帧：
    //   frame[8] = EIP  (用户返回地址)
    //   frame[9] = CS   (代码段)
    //   frame[10] = EFLAGS
    //   frame[11] = ESP (用户栈指针)
    //   frame[12] = SS  (栈段)
    //
    // 我们需要修改这些值，让系统调用返回时跳转到新程序
    
    {
#if defined(ARCH_ARM64)
        // ARM64: 修改 SVC 返回帧
        // 栈帧布局（vectors.S kernel_entry / svc.S）：
        //   frame[0-30]  = X0-X30 (general purpose registers)
        //   frame[31]    = SP_EL0 (user stack pointer)
        //   frame[32]    = ELR_EL1 (user PC / return address)
        //   frame[33]    = SPSR_EL1 (user PSTATE)
        
        // Clear all general-purpose registers for security (prevent kernel data leak)
        for (int i = 0; i < 31; i++) {
            frame[i] = 0;
        }
        
        frame[31] = current->user_stack;   // SP_EL0 = 用户栈顶
        frame[32] = entry_point;           // ELR_EL1 = 新程序入口点
        frame[33] = ARM64_PSTATE_EL0t;     // SPSR_EL1 = 用户模式，中断使能
        
        LOG_DEBUG_MSG("syscall::Process::exec: modified ARM64 syscall frame:\n");
        LOG_DEBUG_MSG("  ELR_EL1 (PC) = 0x%llx\n", (unsigned long long)entry_point);
        LOG_DEBUG_MSG("  SP_EL0 = 0x%llx\n", (unsigned long long)current->user_stack);
        LOG_DEBUG_MSG("  SPSR_EL1 = 0x%llx\n", (unsigned long long)ARM64_PSTATE_EL0t);
#elif defined(ARCH_X86_64)
        // x86_64: 修改 SYSCALL 返回帧
        // 栈帧布局（syscall64_asm.asm）：
        //   frame[12] = RCX (user RIP) - SYSRET 会用这个作为返回地址
        //   frame[4]  = R11 (user RFLAGS)
        //   frame[15] = user RSP
        frame[12] = entry_point;           // RCX = 新程序入口点（SYSRET 返回地址）
        frame[4]  = 0x202;                 // R11 = RFLAGS（中断使能）
        frame[15] = current->user_stack;   // user RSP = 用户栈顶
        
        LOG_DEBUG_MSG("syscall::Process::exec: modified syscall frame to return to 0x%lx\n", (unsigned long)entry_point);
#else
        // i686: 修改 IRET 栈帧
        // 修改用户段寄存器（syscall_handler 会在返回前恢复这些）
        frame[0] = 0x23;   // DS = 用户数据段
        
        // 修改 IRET 栈帧
        frame[8] = entry_point;        // EIP = 新程序入口点
        frame[9] = 0x1B;               // CS = 用户代码段 (Ring 3)
        frame[10] = 0x202;             // EFLAGS = 中断使能
        frame[11] = current->user_stack;  // ESP = 用户栈顶
        frame[12] = 0x23;              // SS = 用户栈段 (Ring 3)
        
        LOG_DEBUG_MSG("syscall::Process::exec: modified syscall frame to return to 0x%lx\n", (unsigned long)entry_point);
#endif
    }
    
    // 返回 0，让系统调用正常返回（通过 iret 到新程序）
    return 0;
}

/**
 * syscall::Process::getpid - 获取当前进程 PID
 */
uint32_t syscall::Process::getpid() {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::getpid: no current task\n");
        return (uint32_t)-1;
    }
    
    LOG_DEBUG_MSG("syscall::Process::getpid: returning PID %u\n", current->pid);
    return current->pid;
}

/**
 * syscall::Process::getppid - 获取父进程 PID
 * @return 父进程 PID，如果没有父进程返回 0
 */
uint32_t syscall::Process::getppid() {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::getppid: no current task\n");
        return 0;
    }
    
    if (current->parent && current->parent->state != TASK_UNUSED) {
        LOG_DEBUG_MSG("syscall::Process::getppid: returning PPID %u\n", current->parent->pid);
        return current->parent->pid;
    }
    
    // 没有父进程（如 init 进程或孤儿进程）
    LOG_DEBUG_MSG("syscall::Process::getppid: no parent, returning 0\n");
    return 0;
}

/**
 * syscall::Process::yield - 主动让出 CPU
 */
uint32_t syscall::Process::yield() {
    LOG_DEBUG_MSG("syscall::Process::yield: yielding CPU\n");
    
    // 调用任务管理器的让出函数
    kernel::Scheduler::yield();
    
    return 0;
}

/**
 * syscall::Process::nanosleep - 睡眠指定时间
 */
uint32_t syscall::Process::nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) {
        LOG_ERROR_MSG("syscall::Process::nanosleep: req is NULL\n");
        return (uint32_t)-1;
    }

    if (req->tv_nsec >= 1000000000u) {
        LOG_ERROR_MSG("syscall::Process::nanosleep: invalid tv_nsec=%u\n", req->tv_nsec);
        return (uint32_t)-1;
    }

    uint64_t total_ns = (uint64_t)req->tv_sec * 1000000000ull + req->tv_nsec;
    uint64_t total_ms = total_ns / 1000000ull;

    if (total_ms == 0 && total_ns > 0) {
        total_ms = 1;
    }

    if (total_ms > 0) {
        if (total_ms > 0xFFFFFFFFull) {
            total_ms = 0xFFFFFFFFull;
        }
        uint32_t sleep_ms = (uint32_t)total_ms;
        kernel::Scheduler::sleep(sleep_ms);
    }

    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }

    return 0;
}

/* kill 接受的信号号上限（不含）。退出状态用低 7 位编码信号号 */
#define KILL_SIGNAL_MAX 64

/**
 * syscall::Process::kill - 向进程发送信号
 *
 * 简化实现：没有信号处理函数，除 0 以外的信号都终止目标进程；
 * 信号 0 只检查目标是否存在。
 *
 * 终止不是由调用者就地完成的。调度是协作式的，别的任务一定停在内核里的
 * 某个 yield/block 点上，可能正持有互斥锁、正排在等待队列里；在这里改它的
 * 状态或释放它的内核栈都会破坏内核。所以只给目标记一个待处理的信号，
 * 由目标自己在系统调用返回用户态前退出（kernel::Scheduler::deliver_pending_kill）。
 * 阻塞在锁上的目标要等到被正常唤醒后才会退出。
 */
uint32_t syscall::Process::kill(uint32_t pid, uint32_t signal) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::kill: no current task\n");
        return (uint32_t)-1;
    }

    LOG_DEBUG_MSG("syscall::Process::kill: PID %u sending signal %u to PID %u\n",
                  current->pid, signal, pid);

    // 不能杀死 idle 进程（PID 0）
    if (pid == 0) {
        LOG_WARN_MSG("syscall::Process::kill: cannot kill idle process (PID 0)\n");
        return (uint32_t)-1;
    }

    if (signal >= KILL_SIGNAL_MAX) {
        LOG_WARN_MSG("syscall::Process::kill: invalid signal %u\n", signal);
        return (uint32_t)-1;
    }

    // 查找目标进程
    task_t *target = kernel::Scheduler::get_by_pid(pid);
    if (!target || target->state == TASK_UNUSED || target->state == TASK_TERMINATED) {
        LOG_DEBUG_MSG("syscall::Process::kill: process %u not found\n", pid);
        return (uint32_t)-1;
    }

    // 内核线程不是用户进程能终止的对象
    if (!target->is_user_process) {
        LOG_WARN_MSG("syscall::Process::kill: PID %u is a kernel thread, refused\n", pid);
        return (uint32_t)-1;
    }

    // 信号 0：只探测目标是否存在，任何进程都可以问
    // （服务进程靠它发现客户已经退出，回收为客户保留的资源）
    if (signal == 0) {
        return 0;
    }

    // 非特权进程只能向自己和自己的子孙进程发信号
    if (!kernel::Scheduler::current_is_privileged() && target != current &&
        !kernel::Scheduler::is_descendant(target, current)) {
        LOG_DEBUG_MSG("syscall::Process::kill: PID %u may not signal PID %u\n", current->pid, pid);
        return (uint32_t)-1;
    }

    // 已经是僵尸：进程已退出，等待父进程回收，视为成功
    if (target->state == TASK_ZOMBIE) {
        LOG_DEBUG_MSG("syscall::Process::kill: process %u is already zombie\n", pid);
        return 0;
    }

    // 杀死自己：这里就是自己的上下文，直接走正常的退出路径（不返回）
    if (target == current) {
        kernel::Scheduler::exit_current(128 + signal, true, signal);
    }

    if (!kernel::Scheduler::request_kill(target, signal)) {
        // 目标在此期间已经退出
        return 0;
    }

    LOG_DEBUG_MSG("syscall::Process::kill: signal %u queued for process %u (%s)\n",
                  signal, pid, target->name);
    return 0;
}

/**
 * syscall::Process::waitpid - 等待子进程退出
 * 
 * @param pid     要等待的进程 PID（-1 表示任意子进程，>0 表示特定进程）
 * @param wstatus 退出状态存储地址（可为 NULL）
 * @param options 等待选项（WNOHANG = 非阻塞）
 * @return 成功返回子进程 PID，没有子进程返回 (uint32_t)-1，WNOHANG 时无退出子进程返回 0
 */
uint32_t syscall::Process::waitpid(int32_t pid, uint32_t *wstatus, uint32_t options) {
    task_t *current = kernel::Scheduler::get_current();
    if (!current) {
        LOG_ERROR_MSG("syscall::Process::waitpid: no current task\n");
        return (uint32_t)-1;
    }
    
    bool non_blocking = (options & WNOHANG) != 0;
    
    LOG_DEBUG_MSG("syscall::Process::waitpid: PID %u waiting for child PID %d (options=%u)\n", 
                  current->pid, pid, options);
    
    // 循环等待，直到找到退出的子进程
    while (true) {
        bool prev_state = kernel::Interrupts::disable();
        
        // 查找符合条件的子进程
        task_t *found_child = NULL;
        bool has_waited_child = false;
        
        // 遍历所有任务，查找子进程
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            task_t *task = &task_pool[i];
            
            // 跳过未使用的任务
            if (task->state == TASK_UNUSED) {
                continue;
            }
            
            // 检查是否为当前进程的子进程
            if (task->parent != current) {
                continue;
            }
            
            // 检查 PID 是否匹配
            if (pid > 0 && (int32_t)task->pid != pid) {
                continue;  // 不是我们要等待的特定进程
            }
            
            // 找到了符合条件的子进程（无论状态如何）
            has_waited_child = true;
            
            // 检查是否为僵尸进程（已退出）
            if (task->state == TASK_ZOMBIE) {
                found_child = task;
                break;
            }
        }
        
        // 如果找到了退出的子进程
        if (found_child) {
            uint32_t child_pid = found_child->pid;
            uint32_t status = 0;
            
            // 构造退出状态
            if (found_child->exit_signaled) {
                // 被信号终止：低 7 位 = 信号号（与用户库的 WTERMSIG 一致）
                status = found_child->exit_signal & 0x7F;
            } else {
                // 正常退出：低 8 位 = 0，高 8 位 = 退出码
                status = (found_child->exit_code & 0xFF) << 8;
            }
            
            // 将状态写回用户空间（如果提供了地址）
            if (wstatus != NULL) {
                *wstatus = status;
            }
            
            LOG_DEBUG_MSG("syscall::Process::waitpid: found zombie child PID %u, status=%u\n", 
                         child_pid, status);
            
            // 回收子进程资源
            kernel::Scheduler::free(found_child);
            
            kernel::Interrupts::restore(prev_state);
            return child_pid;
        }
        
        kernel::Interrupts::restore(prev_state);
        
        // 如果没有符合条件的子进程（指定的进程不存在或不是子进程），返回错误
        if (!has_waited_child) {
            if (pid == -1) {
                LOG_DEBUG_MSG("syscall::Process::waitpid: no child processes\n");
            } else {
                LOG_DEBUG_MSG("syscall::Process::waitpid: child PID %d not found or not a child\n", pid);
            }
            return (uint32_t)-1;
        }
        
        // 如果是非阻塞模式且没有退出的子进程，返回 0
        if (non_blocking) {
            LOG_DEBUG_MSG("syscall::Process::waitpid: WNOHANG and no exited children\n");
            return 0;
        }
        
        // 自己被 kill 了（request_kill 会把等子进程的任务唤醒）：返回到系统调用出口去执行退出
        if (current->kill_pending) {
            return (uint32_t)-1;
        }

        // 阻塞到有子进程退出：exit_current 在子进程变成僵尸时唤醒我们
        // （等待对象就是自己的 PCB）。从上面的检查到这里没有调度点，不会错过唤醒。
        kernel::Scheduler::block(current);
    }
}