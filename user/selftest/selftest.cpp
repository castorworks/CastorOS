// selftest - 用户态自检程序
//
// 放在启动文件系统里，由命令行的 rc 脚本在开机时运行（也可以随时手动再跑）。
// 依次检查内存、进程、IPC、特权、共享内存、名字服务和文件服务；
// 每项打印一行结果，有失败时以非零状态退出。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <names.h>
#include <console.h>
#include <fs.h>
#include <blk.h>
#include <net.h>

static int failures = 0;

// 打印一项检查的结果；good 是通过时显示的词（"ok" 或 "refused"）
static void report(const char *what, bool passed, const char *good) {
    printf("selftest: %s: %s\n", what, passed ? good : "FAILED");
    if (!passed) {
        failures++;
    }
}

// 演示用的“服务”协议
enum {
    ADD_REQUEST = 1,    // data[0] + data[1] -> data[0]
    ADD_QUIT    = 2,
};

// 一个最小的服务进程：收请求、处理、把应答发回给请求者
static void add_server(void) {
    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        if (m.label == ADD_QUIT) {
            exit(0);
        }
        m.data[0] += m.data[1];
        ipc_reply(m.sender, &m);
    }
}

static void test_memory_and_fork(void) {
    char *page = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        report("mmap", false, "ok");
        return;
    }
    page[0] = 42;

    // 子进程看到父进程写入的值；它自己的写入落在写时复制的副本上
    int parent = getpid();
    int pid = fork();
    if (pid == 0) {
        int ok = page[0] == 42 && getppid() == parent;
        page[0] = 1;
        usleep(20000);
        exit(ok ? 7 : 1);
    }
    int status = 0;
    int reaped = waitpid(pid, &status, 0);
    report("mmap, fork, copy-on-write, waitpid",
           reaped == pid && WEXITSTATUS(status) == 7 && page[0] == 42, "ok");
    munmap(page, 4096);

    // 访问没有映射的地址：进程被终止，父进程看到的是“被信号 11 终止”（三个架构一样）
    pid = fork();
    if (pid == 0) {
        volatile int *volatile nowhere = (volatile int *)0;
        *nowhere = 1;
        exit(0);        // 写成功了才会走到这里
    }
    status = 0;
    reaped = waitpid(pid, &status, 0);
    report("fault kills the process with signal 11",
           reaped == pid && WIFSIGNALED(status) && WTERMSIG(status) == 11, "ok");
}

static void test_ipc(void) {
    int server = fork();
    if (server == 0) {
        add_server();
    }

    // 请求-应答
    int ok = 1;
    for (int i = 1; i <= 3; i++) {
        struct ipc_msg m = {};
        m.label = ADD_REQUEST;
        m.data[0] = i;
        m.data[1] = 100;
        if (ipc_call(server, &m) != 0 || m.sender != (uint32_t)server || m.data[0] != (uint64_t)(i + 100)) {
            ok = 0;
        }
    }
    report("ipc call x3", ok, "ok");

    // reply 只对正在 call 自己的进程有效，否则立刻失败而不是阻塞
    struct ipc_msg stray = {};
    report("ipc reply to a process that is not calling", ipc_reply(server, &stray) == -1, "refused");

    // 让服务退出；之后再发消息应当失败
    struct ipc_msg quit = {};
    quit.label = ADD_QUIT;
    ipc_send(server, &quit);
    waitpid(server, NULL, 0);
    report("ipc send to exited server", ipc_send(server, &quit) == -1, "refused");

    // 阻塞在 recv 上的进程可以被 kill
    int idle = fork();
    if (idle == 0) {
        struct ipc_msg m;
        ipc_recv(IPC_ANY, &m);
        exit(1);
    }
    usleep(50000);
    kill(idle, 9);
    int status = 0;
    waitpid(idle, &status, 0);
    report("kill of blocked receiver", WIFSIGNALED(status), "ok");
}

static void test_ipc_blocking(void) {
    int self = getpid();
    int status = 0;

    // 发送者先到：对方还没开始接收，send 阻塞到它来取为止
    int late = fork();
    if (late == 0) {
        usleep(30000);
        struct ipc_msg m;
        exit(ipc_recv(self, &m) == 0 && m.label == 42 && m.sender == (uint32_t)self ? 0 : 1);
    }
    struct ipc_msg hello = {};
    hello.label = 42;
    int ok = ipc_send(late, &hello) == 0;
    waitpid(late, &status, 0);
    report("ipc send before the receiver is ready", ok && WEXITSTATUS(status) == 0, "ok");

    // 两个发送者都在排队时，按指定的 PID 接收，不受排队顺序影响
    int senders[2];
    for (int i = 0; i < 2; i++) {
        senders[i] = fork();
        if (senders[i] == 0) {
            struct ipc_msg m = {};
            m.label = (uint32_t)(100 + i);
            exit(ipc_send(self, &m) == 0 ? 0 : 1);
        }
    }
    usleep(30000);
    struct ipc_msg m;
    ok = ipc_recv(senders[1], &m) == 0 && m.sender == (uint32_t)senders[1] && m.label == 101 &&
         ipc_recv(senders[0], &m) == 0 && m.sender == (uint32_t)senders[0] && m.label == 100;
    waitpid(senders[0], NULL, 0);
    waitpid(senders[1], NULL, 0);
    report("ipc receive from a specific sender", ok, "ok");

    // 对方收下请求后没应答就退出：call 带着错误返回，而不是永远等下去
    int quitter = fork();
    if (quitter == 0) {
        struct ipc_msg req;
        ipc_recv(IPC_ANY, &req);
        exit(0);
    }
    struct ipc_msg req = {};
    ok = ipc_call(quitter, &req) == -1;
    waitpid(quitter, NULL, 0);
    report("ipc call to a server that exits without replying", ok, "refused");

    // 对方一直不接收就退出了：阻塞中的 send 同样带着错误返回
    int deaf = fork();
    if (deaf == 0) {
        usleep(30000);
        exit(0);
    }
    ok = ipc_send(deaf, &hello) == -1;
    waitpid(deaf, NULL, 0);
    report("ipc send to a process that exits without receiving", ok, "refused");
}

static void test_timer(void) {
    // 定时器到期时内核发来一条消息；时间确实过去了那么久
    uint64_t start = uptime_ms();
    timer_set(50);
    struct ipc_msg m;
    int ok = ipc_recv(IPC_FROM_KERNEL, &m) == 0 && m.sender == IPC_KERNEL && m.label == IPC_LABEL_TIMER;
    uint64_t elapsed = uptime_ms() - start;
    ok = ok && elapsed >= 40 && elapsed < 500;

    // 取消的定时器不会到期：之后设的短定时器先到，而且只到一次
    timer_set(30);
    timer_set(0);
    timer_set(60);
    start = uptime_ms();
    ok = ok && ipc_recv(IPC_FROM_KERNEL, &m) == 0 && m.label == IPC_LABEL_TIMER && uptime_ms() - start >= 50;
    report("uptime and timer", ok, "ok");
}

static void test_privilege(void) {
    // 本程序没有特权，也没有被许可任何设备：不能访问设备寄存器，不能认领中断，
    // 拿不到 DMA 内存，查不到设备在哪里，也不能给自己加许可
    int pid = fork();
    if (pid == 0) {
        uint32_t v;
        uint64_t phys;
        struct device_info info;
        struct hw_range range;
        exit(io_read(0x80, 1, &v) == -1 && io_read(0x3F8, 1, &v) == -1 &&
             irq_claim(5) == -1 && irq_claim(40) == -1 &&
             map_device(0xB8000, 4096) == MAP_FAILED && map_device(0x09000000, 4096) == MAP_FAILED &&
             dma_alloc(4096, &phys) == MAP_FAILED &&
             device_find("arm,pl011", 0, &info) == -1 && device_find("virtio,mmio", 0, &info) == -1 &&
             hw_allowed(0, &range) == -1 && !hw_find(HW_IRQ, 0, &range) &&
             hw_allow(HW_PORTS, 0x80, 1) == -1 && hw_allow(HW_IRQ, 5, 1) == -1 &&
             io_read(0x80, 1, &v) == -1 && irq_claim(5) == -1 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    report("hardware access without privilege", WEXITSTATUS(status) == 0, "refused");

#if defined(ARCH_I686) || defined(ARCH_X86_64)
    // 驱动直接用 in/out 指令访问许可给它的端口（内核在 CPU 的 I/O 许可位图里打开了它们）。
    // 没被许可的端口在位图里是关着的：绕过系统调用直接碰，CPU 不让，进程被终止
    pid = fork();
    if (pid == 0) {
        uint8_t v;
        __asm__ volatile("inb $0x80, %0" : "=a"(v));
        exit(0);        // 读成功了才会走到这里
    }
    status = 0;
    waitpid(pid, &status, 0);
    report("direct port access without permission", WIFSIGNALED(status) || WEXITSTATUS(status) != 0, "refused");
#endif
}

// 把一个值放进一个浮点寄存器，过一会儿再取出来。值只在寄存器里，不经过内存：
// 这期间如果被换下 CPU，它能不能原样回来全靠内核保存和恢复这些寄存器
#if defined(ARCH_ARM64)
static void fp_reg_set(uint64_t v) {
    __asm__ volatile("fmov d15, %0" : : "r"(v));
}
static uint64_t fp_reg_get(void) {
    uint64_t v;
    __asm__ volatile("fmov %0, d15" : "=r"(v));
    return v;
}
#elif defined(ARCH_X86_64)
static void fp_reg_set(uint64_t v) {
    __asm__ volatile("movq %0, %%xmm15" : : "r"(v));
}
static uint64_t fp_reg_get(void) {
    uint64_t v;
    __asm__ volatile("movq %%xmm15, %0" : "=r"(v));
    return v;
}
#else
// i686 的程序用 x87：压进它的寄存器栈，取的时候弹出来（63 位以内的整数在里面是精确的）
static void fp_reg_set(uint64_t v) {
    __asm__ volatile("fildll %0" : : "m"(v));
}
static uint64_t fp_reg_get(void) {
    uint64_t v;
    __asm__ volatile("fistpll %0" : "=m"(v));
    return v;
}
#endif

static void test_floating_point(void) {
    // 父子两个进程各把自己的值留在同一个浮点寄存器里，然后让出 CPU，对方上来放它的值
    int pid = fork();
    uint64_t mine = pid == 0 ? 0x1111222233334444ULL : 0x0555666677778888ULL;
    bool ok = true;
    for (uint64_t i = 0; i < 200 && ok; i++) {
        fp_reg_set(mine + i);
        yield();
        ok = fp_reg_get() == mine + i;
    }
    // 不主动让出、被时钟中断换下去的时候也一样
    for (uint64_t i = 0; i < 4 && ok; i++) {
        fp_reg_set(mine - i);
        uint64_t start = uptime_ms();
        while (uptime_ms() - start < 30) {
        }
        ok = fp_reg_get() == mine - i;
    }
    if (pid == 0) {
        exit(ok ? 0 : 1);
    }
    int status = 1;
    waitpid(pid, &status, 0);
    report("floating-point registers across context switches", ok && WEXITSTATUS(status) == 0, "kept");

    // 浮点运算和 %f（volatile：让运算在运行时做，而不是编译器算好）
    volatile double a = 1.5, b = 2.75, pi = 3.14159;
    char text[48];
    snprintf(text, sizeof(text), "%.2f|%f|%8.3f|%.0f", pi, -a / 4, -a, a + 1.1);
    report("floating-point arithmetic and %f",
           a * b == 4.125 && (int)(a * b * 8) == 33 && strcmp(text, "3.14|-0.375000|  -1.500|3") == 0, "ok");

    // 用户库的数学函数（详细的检查在宿主机上跑：make lib-test；这里确认它们在真的目标上也对）
    volatile double two = 2.0, x = 0.7;
    double root = sqrt(two);
    double near[] = { root * root - 2.0, sin(M_PI / 6) - 0.5, sin(x) * sin(x) + cos(x) * cos(x) - 1.0,
                      log(exp(x)) - x, pow(two, 0.5) - root };
    ok = pow(two, 10) == 1024 && floor(-x) == -1 && ceil(x) == 1;
    for (size_t i = 0; i < sizeof(near) / sizeof(near[0]); i++) {
        ok = ok && fabs(near[i]) < 1e-12;
    }
    report("math functions", ok, "ok");
}

static void test_cpus(void) {
    uint32_t count = 0;
    int here = cpu_info(&count);
    bool ok = count >= 1 && here >= 0 && (uint32_t)here < count;
    printf("selftest: %u cpu%s\n", count, count == 1 ? "" : "s");
    if (count < 2) {
        report("cpu count", ok, "ok");
        return;
    }

    // 几个 CPU：每个 CPU 一个进程，各自埋头算 300 毫秒，记下自己都在哪些 CPU 上待过。
    // 合起来要不止一个 CPU 运行过用户进程，而且它们是同时在算：全部算完用的时间
    // 远少于一个接一个算的时间（count * 300 毫秒）
    int children[8];
    uint32_t n = count > 8 ? 8 : count;
    uint64_t start = uptime_ms();
    for (uint32_t i = 0; i < n; i++) {
        children[i] = fork();
        if (children[i] == 0) {
            uint32_t seen = 0;
            uint64_t began = uptime_ms();
            volatile uint64_t work = 0;
            while (uptime_ms() - began < 300) {
                for (int k = 0; k < 10000; k++) {
                    work = work + (uint64_t)k;
                }
                seen |= 1u << cpu_info(NULL);
            }
            exit((int)(seen & 0xFF));
        }
    }
    uint32_t seen = 0;
    for (uint32_t i = 0; i < n; i++) {
        int status = 0;
        waitpid(children[i], &status, 0);
        seen |= (uint32_t)WEXITSTATUS(status);
    }
    uint64_t elapsed = uptime_ms() - start;
    uint32_t used = 0;
    for (uint32_t cpu = 0; cpu < 8; cpu++) {
        used += (seen >> cpu) & 1;
    }
    ok = ok && used >= 2 && elapsed < (uint64_t)n * 300 * 3 / 4;
    if (!ok) {
        printf("selftest: (%u cpus, user code ran on %u of them, %u ms for %u x 300 ms of work)\n",
               count, used, (unsigned)elapsed, n);
    }
    report("processes run on several cpus at once", ok, "ok");

    // 一个 CPU 让任务变成就绪时，闲着的 CPU 是被立刻叫醒的，不是等到自己的下一次时钟中断
    // （最多 10 毫秒）才发现。子进程向我们发请求然后等应答；我们应答（它变成就绪）之后
    // 不让出 CPU，继续占着算 15 毫秒，所以它只能由别的、正闲着的 CPU 接手。应答里带着
    // 应答那一刻的时间，子进程醒来后看过了多久，报告 30 次里有几次等了 2 毫秒以上。
    // 靠时钟中断发现的话十次里有八次要等这么久；被叫醒的话几乎一次都没有（偶尔别的 CPU
    // 正忙着别的进程，那一次会久一些，所以数次数而不是把时间加起来）
    int waiter = fork();
    if (waiter == 0) {
        int slow = 0;
        for (int i = 0; i < 30; i++) {
            struct ipc_msg m = {};
            m.label = 1;
            if (ipc_call(getppid(), &m) != 0) {
                exit(255);
            }
            slow += uptime_ms() - m.data[0] >= 2;
        }
        exit(slow);
    }
    for (int i = 0; i < 30; i++) {
        struct ipc_msg m;
        if (ipc_recv(waiter, &m) != 0) {
            break;
        }
        struct ipc_msg reply = {};
        reply.label = 1;
        uint64_t now = uptime_ms();
        reply.data[0] = now;
        ipc_reply(waiter, &reply);
        while (uptime_ms() - now < 15) {
        }
    }
    int status = 0;
    waitpid(waiter, &status, 0);
    int slow = WEXITSTATUS(status);
    ok = slow < 10;
    if (!ok) {
        printf("selftest: (a woken task waited 2 ms or more in %d of 30 rounds)\n", slow);
    }
    report("idle cpus are woken for a ready task", ok, "ok");
}

// 一个进程反复做的事：要内存、写满、长堆缩堆、fork、把一页共享给父进程、还内存。
// 每一步都核对内容。@return 一切都对
static bool memory_worker(int parent, uint32_t seed) {
    for (uint32_t round = 0; round < 120; round++) {
        seed = seed * 1103515245u + 12345u;
        size_t pages = 1 + (seed >> 16) % 8;
        uint32_t *block = (uint32_t *)mmap(NULL, pages * 4096, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            return false;
        }
        // 新映射的页是清零的；每页写上只有自己知道的值
        for (size_t i = 0; i < pages; i++) {
            if (block[i * 1024] != 0 || block[i * 1024 + 1023] != 0) {
                return false;
            }
            block[i * 1024] = seed + (uint32_t)i;
            block[i * 1024 + 1023] = ~(seed + (uint32_t)i);
        }

        if (round % 3 == 0) {
            // 堆长三页、写、缩回去
            char *top = (char *)sbrk(3 * 4096);
            if (top == (char *)-1) {
                return false;
            }
            top[0] = (char)seed;
            top[3 * 4096 - 1] = (char)~seed;
            if (top[0] != (char)seed || sbrk(-3 * 4096) == (void *)-1) {
                return false;
            }
        }
        if (round % 10 == 5) {
            // 子进程改它那一份（写时复制），我们这一份不能变
            int child = fork();
            if (child == 0) {
                for (size_t i = 0; i < pages; i++) {
                    block[i * 1024] = 0xDEADBEEFu;
                }
                exit(block[1023] == ~seed ? 0 : 1);
            }
            int status = 1;
            waitpid(child, &status, 0);
            if (WEXITSTATUS(status) != 0) {
                return false;
            }
        }
        if (round % 8 == 7) {
            // 把第一页共享给父进程：它核对内容，在里面留个记号，然后撤掉它那边的映射
            if (mem_grant(parent, block, 4096) != 0) {
                return false;
            }
            struct ipc_msg ask = {};
            ask.label = 1;
            if (ipc_call(parent, &ask) != 0 || block[1] != (uint32_t)parent) {
                return false;
            }
            block[1] = 0;
        }

        for (size_t i = 0; i < pages; i++) {
            if (block[i * 1024] != seed + (uint32_t)i || block[i * 1024 + 1023] != ~(seed + (uint32_t)i)) {
                return false;
            }
        }
        if (munmap(block, pages * 4096) != 0) {
            return false;
        }
    }
    return true;
}

/** 启动 workers 个 memory_worker，等它们做完，期间收它们共享过来的页。@return 内容都对 */
static bool run_memory_workers(int workers) {
    int self = getpid();
    int pids[6];
    for (int i = 0; i < workers; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            bool ok = memory_worker(self, 0x1234567u * (uint32_t)(i + 1));
            struct ipc_msg done = {};
            done.label = 2;
            done.data[0] = ok;
            ipc_call(self, &done);
            exit(ok ? 0 : 1);
        }
    }

    // 等它们做完；这期间收它们共享过来的页
    bool ok = true;
    uintptr_t granted[6] = {};
    for (int finished = 0; finished < workers; ) {
        struct ipc_msg m;
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }
        int slot = -1;
        for (int i = 0; i < workers; i++) {
            if (pids[i] == (int)m.sender) {
                slot = i;
            }
        }
        if (slot < 0) {
            continue;
        }
        struct ipc_msg reply = {};
        if (m.label == IPC_LABEL_GRANT) {
            granted[slot] = (uintptr_t)m.data[0];       // 映射在这里；等它来问的时候再看
        } else if (m.label == 1) {
            uint32_t *page = (uint32_t *)granted[slot];
            ok = ok && page != NULL && page[1023] != 0;
            if (page) {
                page[1] = (uint32_t)self;
                munmap(page, 4096);
                granted[slot] = 0;
            }
            reply.label = 1;
            ipc_reply(m.sender, &reply);
        } else if (m.label == 2) {
            ok = ok && m.data[0] != 0;
            finished++;
            reply.label = 2;
            ipc_reply(m.sender, &reply);
        }
    }
    for (int i = 0; i < workers; i++) {
        int status = 1;
        waitpid(pids[i], &status, 0);
        ok = ok && WEXITSTATUS(status) == 0;
    }
    return ok;
}

static void test_parallel_memory(void) {
    // 内存的系统调用（mmap、munmap、brk）不拿内核锁，几个 CPU 上的进程可以同时在里面。
    // 让几个进程同时反复要内存、还内存，中间夹着 fork（写时复制）和共享内存（别的进程往
    // 自己的地址空间里放映射）。每个进程看到的内容都得是自己写的，最后一页内存都不能少
    uint32_t cpus = 1;
    cpu_info(&cpus);
    int workers = cpus < 2 ? 2 : cpus > 6 ? 6 : (int)cpus;

    // 跑两遍，数第二遍前后的空闲页。第一遍里我们自己的地址空间第一次收到共享来的页，
    // 内核要给那段地址建页表，那一页到我们退出才还——它不是漏掉的
    bool ok = run_memory_workers(workers);
    long before = mem_free_pages();
    ok = run_memory_workers(workers) && ok;

    // 退出的进程的内存是延迟一点才收回的：等它稳定下来
    long after = mem_free_pages();
    for (int i = 0; i < 50 && after != before; i++) {
        usleep(20000);
        after = mem_free_pages();
    }
    if (!ok || after != before) {
        printf("selftest: (%d workers, contents %s, free pages %ld -> %ld)\n", workers,
               ok ? "ok" : "WRONG", before, after);
    }
    report("memory system calls from several processes at once", ok && after == before, "ok");
}

#define PIPC_ROUNDS  1500
#define PIPC_QUIT    99

/**
 * 并行 IPC 检查里的一个客户：三种通信混着做。
 *   - 向大家共用的服务 call（服务那边几个 CPU 上的客户同时排着队）
 *   - 和自己专属的伙伴 send / recv 一来一回（两个进程各在一个 CPU 上互相等、互相叫醒）
 *   - 每隔一阵等一次定时器（内核从时钟中断里叫醒一个正在 recv 的进程）
 * @return 每个回答都是对的、都是给自己的
 */
static bool ipc_client(int server, uint32_t seed) {
    int self = getpid();
    int partner = fork();
    if (partner == 0) {
        // 伙伴：收到 v 就回 v + 1，收到 PIPC_QUIT 结束
        for (;;) {
            struct ipc_msg m;
            if (ipc_recv(self, &m) != 0 || m.label == PIPC_QUIT) {
                exit(0);
            }
            m.data[0] += 1;
            if (ipc_send(self, &m) != 0) {
                exit(1);
            }
        }
    }
    if (partner < 0) {
        return false;
    }

    bool ok = true;
    uint32_t v = seed;
    for (int i = 0; i < PIPC_ROUNDS && ok; i++) {
        v = v * 1664525u + 1013904223u;

        struct ipc_msg m = {};
        m.label = 1;
        m.data[0] = v;
        m.data[1] = (uint64_t)self;
        ok = ipc_call(server, &m) == 0 && m.data[0] == (uint64_t)v * 3 + 1 &&
             m.data[1] == (uint64_t)self && m.sender == (uint32_t)server;

        m = {};
        m.label = 2;
        m.data[0] = v;
        ok = ok && ipc_send(partner, &m) == 0 && ipc_recv(partner, &m) == 0 &&
             m.data[0] == (uint64_t)v + 1 && m.sender == (uint32_t)partner;

        if (i % 300 == 299) {
            timer_set(1);
            ok = ok && ipc_recv(IPC_FROM_KERNEL, &m) == 0 && m.label == IPC_LABEL_TIMER;
        }
    }

    struct ipc_msg quit = {};
    quit.label = PIPC_QUIT;
    ipc_send(partner, &quit);
    int status = -1;
    waitpid(partner, &status, 0);
    return ok && status == 0;
}

static void test_parallel_ipc(void) {
    // send / recv / call / reply 不拿内核锁：几个 CPU 上的进程可以同时在里面，互相交消息、
    // 互相叫醒。让几个客户同时围着一个服务转，各自还带一个伙伴来回传球。任何一次"刚决定
    // 要睡，对方已经叫过了"都会让某个进程永远醒不过来（这项检查就做不完）；任何一条消息
    // 交错了人，回答就对不上
    uint32_t cpus = 1;
    cpu_info(&cpus);
    int clients = cpus < 2 ? 2 : cpus > 6 ? 6 : (int)cpus;

    int server = fork();
    if (server == 0) {
        for (;;) {
            struct ipc_msg m;
            if (ipc_recv(IPC_ANY, &m) != 0) {
                continue;
            }
            if (m.label == PIPC_QUIT) {
                exit(0);
            }
            // 回答里带着问的人自己报的 PID：服务把回答交错了人的话对不上
            m.data[0] = m.data[0] * 3 + 1;
            ipc_reply(m.sender, &m);
        }
    }

    int pids[6];
    for (int i = 0; i < clients; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            exit(ipc_client(server, 0x9E3779B9u * (uint32_t)(i + 1)) ? 0 : 1);
        }
    }
    bool ok = server > 0;
    for (int i = 0; i < clients; i++) {
        int status = -1;
        ok = pids[i] > 0 && waitpid(pids[i], &status, 0) == pids[i] && status == 0 && ok;
    }

    // 一个进程正等着服务的回答时服务退出了：它得被叫醒并得到失败，而不是一直等下去。
    // 这里的服务故意收了不回
    int silent = fork();
    if (silent == 0) {
        struct ipc_msg m;
        ipc_recv(IPC_ANY, &m);
        usleep(20000);
        exit(0);
    }
    struct ipc_msg m = {};
    m.label = 1;
    ok = ok && silent > 0 && ipc_call(silent, &m) == -1;
    waitpid(silent, NULL, 0);

    struct ipc_msg quit = {};
    quit.label = PIPC_QUIT;
    ipc_send(server, &quit);
    waitpid(server, NULL, 0);
    // 退出了的进程收不到消息
    ok = ok && ipc_send(server, &quit) == -1;

    if (!ok) {
        printf("selftest: (%d clients)\n", clients);
    }
    report("messages between several processes at once", ok, "ok");
}

static void test_shared_memory(void) {
    // 父进程把一页内存共享给子进程；内核用一条 IPC_LABEL_GRANT 消息告诉子进程映射在哪。
    // 子进程经由共享映射写入，父进程能看到（fork 得到的那份只是写时复制的副本）
    volatile uint32_t *page = (volatile uint32_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    page[0] = 1;

    int child = fork();
    if (child == 0) {
        struct ipc_msg m;
        ipc_recv(getppid(), &m);            // 内核发来的授予通知
        if (m.label != IPC_LABEL_GRANT || m.data[1] != 4096) {
            exit(1);
        }
        volatile uint32_t *shared = (volatile uint32_t *)(uintptr_t)m.data[0];
        uint32_t seen = shared[0];
        shared[0] = 2;
        page[0] = 99;                       // 这是子进程自己的副本，父进程看不到

        ipc_recv(getppid(), &m);            // 父进程来问结果
        m.data[0] = seen;
        ipc_reply(m.sender, &m);
        exit(0);
    }

    struct ipc_msg m = {};
    int ok = mem_grant(child, (void *)page, 4096) == 0 &&
             ipc_call(child, &m) == 0 && m.data[0] == 1 && page[0] == 2;
    waitpid(child, NULL, 0);

    // 对方退出后这一页仍然属于自己；向已退出的进程授予会失败
    page[0] = 3;
    ok = ok && page[0] == 3 && mem_grant(child, (void *)page, 4096) == -1;

    // 用户进程发不出内核保留的 label
    struct ipc_msg forged = {};
    forged.label = IPC_LABEL_GRANT;
    ok = ok && ipc_send(NAME_SERVER_PID, &forged) == -1;

    munmap((void *)page, 4096);
    report("shared memory", ok, "ok");
}

static void test_names(void) {
    // 名字服务：登记、查询、重复登记被拒绝
    int ok = name_register("selftest") == 0 && name_lookup("selftest") == getpid() &&
             name_register("selftest") == -1 && name_lookup("no-such-service") == 0;
    report("name service", ok, "ok");

    // 登记者退出后名字失效，别人可以重新登记
    int child = fork();
    if (child == 0) {
        exit(name_register("selftest-child") == 0 ? 0 : 1);
    }
    int status = 1;
    waitpid(child, &status, 0);
    ok = WEXITSTATUS(status) == 0 && name_lookup("selftest-child") == 0;
    child = fork();
    if (child == 0) {
        exit(name_register("selftest-child") == 0 ? 0 : 1);
    }
    waitpid(child, &status, 0);
    report("names of exited processes are released", ok && WEXITSTATUS(status) == 0, "ok");

    // 模块的服务名是留给 init 启动的那个进程的：别人登记不了，不管那个服务现在在不在
    // （没有磁盘时 blk 和 diskfs 已经退出了，名字也不让给别人）
    ok = name_register(CONSOLE_SERVICE_NAME) == -1 && name_register(BLK_SERVICE_NAME) == -1 &&
         name_register(NET_SERVICE_NAME) == -1 && name_register(FS_SERVICE_NAME) == -1 &&
         name_register(FS_DISK_SERVICE_NAME) == -1 && name_lookup(CONSOLE_SERVICE_NAME) > 0 && name_lookup(FS_SERVICE_NAME) > 0;
    report("service names of modules cannot be taken", ok, "refused");
}

static void test_fs(void) {
    // 文件服务（user/ramfs）：内容经共享缓冲区传递，一次读写会被拆成多个请求
    static char out[10000], in[10000];
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 7 + i / 251);
    }

    int fd = fs_open("selftest.dat", FS_O_CREATE | FS_O_TRUNC);
    int ok = fd >= 0 &&
             fs_write(fd, 0, out, sizeof(out)) == (long)sizeof(out) &&
             fs_read(fd, 0, in, sizeof(in)) == (long)sizeof(in) &&
             memcmp(out, in, sizeof(out)) == 0 &&
             fs_read(fd, 9990, in, 100) == 10 && memcmp(out + 9990, in, 10) == 0 &&
             fs_open("no-such-file", 0) == -1;

    // 能在列表里找到它，大小正确
    char name[FS_NAME_MAX];
    uint32_t size = 0;
    int listed = 0;
    for (int i = 0; fs_list("", i, name, &size, NULL) == 0; i++) {
        if (strcmp(name, "selftest.dat") == 0 && size == sizeof(out)) {
            listed = 1;
        }
    }

    // 另一个进程自己建立连接后能读到同一个文件，但用不了别人的句柄
    int child = fork();
    if (child == 0) {
        char byte = 0;
        int mine = fs_open("selftest.dat", 0);
        exit(mine >= 0 && fs_read(mine, 5000, &byte, 1) == 1 && byte == out[5000] &&
             fs_read(fd, 0, &byte, 1) == -1 ? 0 : 1);
    }
    int status = 1;
    waitpid(child, &status, 0);

    ok = ok && listed && WEXITSTATUS(status) == 0 &&
         fs_close(fd) == 0 && fs_read(fd, 0, in, 1) == -1 &&
         fs_unlink("selftest.dat") == 0 && fs_open("selftest.dat", 0) == -1;
    report("file service", ok, "ok");
}

/** 目录 dir 里有没有叫 name 的一项，并且它是/不是目录 */
static bool dir_has(const char *dir, const char *name, bool want_dir) {
    char found[FS_NAME_MAX];
    bool is_dir;
    for (int i = 0; fs_list(dir, i, found, NULL, &is_dir) == 0; i++) {
        if (strcmp(found, name) == 0) {
            return is_dir == want_dir;
        }
    }
    return false;
}

/** 目录 dir 里有几项；不是目录时是 0 */
static int dir_count(const char *dir) {
    char found[FS_NAME_MAX];
    int n = 0;
    while (fs_list(dir, n, found, NULL, NULL) == 0) {
        n++;
    }
    return n;
}

// 目录：两个文件服务的规则一样（都在 fs_server 里），root 是 "" 或者 "disk:"
static bool check_directories(const char *root) {
    char d[FS_NAME_MAX], sub[FS_NAME_MAX], f1[FS_NAME_MAX], f2[FS_NAME_MAX], deep[FS_NAME_MAX];
    snprintf(d, sizeof(d), "%sstdir", root);
    snprintf(sub, sizeof(sub), "%sstdir/sub", root);
    snprintf(f1, sizeof(f1), "%sstdir/one.txt", root);
    snprintf(f2, sizeof(f2), "%sstdir/sub/two.txt", root);
    snprintf(deep, sizeof(deep), "%sstdir/missing/three.txt", root);
    char in[16];

    // 建目录：上一级得先有；同名的东西已经在了就不行
    bool ok = fs_mkdir(sub) == -1 && fs_mkdir(d) == 0 && fs_mkdir(d) == -1 && fs_mkdir(sub) == 0;
    ok = ok && dir_has(root, "stdir", true) && dir_has(d, "sub", true) && dir_count(d) == 1;

    // 文件只能建在已经存在的目录里；目录不能当文件打开
    int fd = fs_open(f1, FS_O_CREATE);
    ok = ok && fd >= 0 && fs_write(fd, 0, "one", 3) == 3 && fs_close(fd) == 0;
    fd = fs_open(f2, FS_O_CREATE);
    ok = ok && fd >= 0 && fs_write(fd, 0, "second", 6) == 6 && fs_close(fd) == 0;
    ok = ok && fs_open(deep, FS_O_CREATE) == -1 && fs_open(d, 0) == -1 && fs_open(d, FS_O_CREATE) == -1;

    // 一个目录只列出它直接的成员，名字不带目录部分；别的目录里同名的文件是另一个文件
    uint32_t size = 0;
    char found[FS_NAME_MAX];
    ok = ok && dir_count(d) == 2 && dir_has(d, "one.txt", false) && dir_has(d, "sub", true) &&
         dir_count(sub) == 1 && fs_list(sub, 0, found, &size, NULL) == 0 &&
         strcmp(found, "two.txt") == 0 && size == 6 && !dir_has(root, "one.txt", false) &&
         dir_count(f1) == 0;                                 // 文件不是目录
    fd = fs_open(f2, 0);
    ok = ok && fd >= 0 && fs_read(fd, 0, in, sizeof(in)) == 6 && memcmp(in, "second", 6) == 0 && fs_close(fd) == 0;

    // 路径怎么写：多余的 '/' 不算数，"." 是所在的目录，".." 是上一级；根没有上一级
    static const char *const same[] = { "%s/stdir/one.txt/", "%sstdir/./one.txt", "%sstdir/sub/../one.txt",
                                        "%sstdir//one.txt", "%sstdir/sub/./../../stdir/one.txt" };
    char odd[FS_NAME_MAX];
    for (size_t i = 0; i < sizeof(same) / sizeof(same[0]); i++) {
        snprintf(odd, sizeof(odd), same[i], root);
        fd = fs_open(odd, 0);
        ok = ok && fd >= 0 && fs_size(fd) == 3 && fs_close(fd) == 0;
    }
    snprintf(odd, sizeof(odd), "%sstdir/../../one.txt", root);
    ok = ok && fs_open(odd, 0) == -1;

    // 当前目录：换进去之后，不带前缀、不以 '/' 开头的路径从那里算起；
    // 以 '/' 开头的从同一个文件系统的根算起。不是目录的地方换不进去
    char cwd[FS_NAME_MAX + 8], want[FS_NAME_MAX + 8];
    ok = ok && fs_chdir(f1) == -1 && fs_chdir(deep) == -1 && fs_chdir(sub) == 0;
    fs_getcwd(cwd);
    snprintf(want, sizeof(want), "%s/stdir/sub", root[0] ? root : "");
    ok = ok && strcmp(cwd, want) == 0;
    fd = fs_open("two.txt", 0);
    ok = ok && fd >= 0 && fs_size(fd) == 6 && fs_close(fd) == 0;
    fd = fs_open("../one.txt", 0);
    ok = ok && fd >= 0 && fs_size(fd) == 3 && fs_close(fd) == 0;
    fd = fs_open("/stdir/one.txt", 0);
    ok = ok && fd >= 0 && fs_size(fd) == 3 && fs_close(fd) == 0;
    ok = ok && dir_count("") == 1 && dir_count("..") == 2 && fs_open("one.txt", 0) == -1;
    ok = fs_chdir(FS_RAM_PREFIX) == 0 && ok;        // 回到开始的地方，不管上面成没成
    fs_getcwd(cwd);
    ok = ok && strcmp(cwd, "/") == 0;

    // 改名和移动：文件改名、移进别的目录；目录连同里面的东西一起移；不能盖掉已有的东西，
    // 不能移到不存在的目录里，不能把目录移进它自己
    char moved[FS_NAME_MAX], d2[FS_NAME_MAX], inside[FS_NAME_MAX];
    snprintf(moved, sizeof(moved), "%sstdir/sub/moved.txt", root);
    snprintf(d2, sizeof(d2), "%sstdir2", root);
    snprintf(inside, sizeof(inside), "%sstdir/sub/deeper", root);
    ok = ok && fs_rename(f1, f2) == -1 && fs_rename(f1, deep) == -1 && fs_rename(d, inside) == -1 &&
         fs_rename(f1, moved) == 0 && fs_open(f1, 0) == -1 && dir_count(sub) == 2 && dir_count(d) == 1;
    fd = fs_open(moved, 0);
    ok = ok && fd >= 0 && fs_read(fd, 0, in, sizeof(in)) == 3 && memcmp(in, "one", 3) == 0 && fs_close(fd) == 0;
    ok = ok && fs_rename(d, d2) == 0 && !dir_has(root, "stdir", true) && dir_has(root, "stdir2", true);
    snprintf(odd, sizeof(odd), "%sstdir2/sub/two.txt", root);
    fd = fs_open(odd, 0);
    ok = ok && fd >= 0 && fs_size(fd) == 6 && fs_close(fd) == 0;
    ok = ok && fs_rename(d2, d) == 0 && fs_rename(moved, f1) == 0;      // 放回原处，下面照旧清理

    // 删：目录空了才能删
    ok = ok && fs_unlink(d) == -1 && fs_unlink(sub) == -1 &&
         fs_unlink(f2) == 0 && fs_unlink(sub) == 0 && fs_unlink(d) == -1 &&
         fs_unlink(f1) == 0 && fs_unlink(d) == 0 && !dir_has(root, "stdir", true) && fs_mkdir(d) == 0 &&
         fs_unlink(d) == 0;
    return ok;
}

static void test_fs_client_reclaim(void) {
    // 文件服务同时只能记住 16 个客户；已经退出的客户要被回收，否则第 17 个就连不上了
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        int child = fork();
        if (child == 0) {
            int fd = fs_open("rc", 0);
            exit(fd >= 0 && fs_size(fd) > 0 ? 0 : 1);
        }
        int status = 1;
        waitpid(child, &status, 0);
        if (WEXITSTATUS(status) != 0) {
            ok = 0;
        }
    }
    report("file service reclaims exited clients", ok, "ok");
}

static void test_block_device(void) {
    // 驱动与我们同时启动，给它一点时间登记；没有磁盘时它会直接退出
    for (int i = 0; i < 25 && name_lookup(BLK_SERVICE_NAME) == 0; i++) {
        usleep(20000);
    }
    uint64_t sectors = blk_capacity();
    if (sectors == 0) {
        printf("selftest: block device: skipped (no disk)\n");
        return;
    }

    static char saved[16 * BLK_SECTOR_SIZE], out[16 * BLK_SECTOR_SIZE], in[16 * BLK_SECTOR_SIZE];
    uint64_t start = sectors - 16;

    // 盘上是别的东西（真机的硬盘上多半是另一个系统）就一个字节也不写：只读
    bool ours = blk_read(0, in, 8) == 0 && memcmp(in, FS_DISK_MAGIC, FS_DISK_MAGIC_SIZE) == 0;
    if (!ours) {
        ours = true;
        for (size_t i = 0; i < 8 * BLK_SECTOR_SIZE; i++) {
            ours = ours && in[i] == 0;
        }
    }
    if (!ours) {
        report("block device", sectors >= 32 && blk_read(start, in, 16) == 0 && blk_read(sectors, in, 1) == -1,
               "ok (read only: the disk is not ours)");
        return;
    }

    // 在最后 16 个扇区上写一个图案再读回来（跨多个请求），然后恢复原来的内容
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 13 + i / 97);
    }
    int ok = sectors >= 32 &&
             blk_read(start, saved, 16) == 0 &&
             blk_write(start, out, 16) == 0 &&
             blk_read(start, in, 16) == 0 && memcmp(out, in, sizeof(out)) == 0 &&
             blk_write(start, saved, 16) == 0 &&
             blk_read(sectors, in, 1) == -1;        // 越界
    report("block device", ok, "ok");
}

static void test_disk_fs(void) {
    if (blk_capacity() == 0) {
        printf("selftest: disk file system: skipped (no disk)\n");
        return;
    }
    // diskfs 要等块设备驱动就绪后才挂载
    for (int i = 0; i < 100 && name_lookup(FS_DISK_SERVICE_NAME) == 0; i++) {
        usleep(20000);
    }
    if (name_lookup(FS_DISK_SERVICE_NAME) == 0) {
        // 有磁盘但没有文件服务：盘上是别的东西，diskfs 不去动它
        printf("selftest: disk file system: skipped (the disk is not ours)\n");
        return;
    }

    // 跨多个块的文件：写、读回、从中间读、大小、列表、清空、删除
    static char out[10000], in[10000];
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 11 + i / 127);
    }
    const char *path = FS_DISK_PREFIX "selftest.tmp";
    int fd = fs_open(path, FS_O_CREATE | FS_O_TRUNC);
    int ok = fd >= 0 &&
             fs_write(fd, 0, out, sizeof(out)) == (long)sizeof(out) &&
             fs_size(fd) == (long)sizeof(out) &&
             fs_read(fd, 0, in, sizeof(in)) == (long)sizeof(in) && memcmp(out, in, sizeof(out)) == 0 &&
             fs_read(fd, 4090, in, 20) == 20 && memcmp(out + 4090, in, 20) == 0 &&
             fs_write(fd, 4090, "0123456789", 10) == 10 &&
             fs_read(fd, 4085, in, 20) == 20 && memcmp(in, out + 4085, 5) == 0 &&
             memcmp(in + 5, "0123456789", 10) == 0 && memcmp(in + 15, out + 4100, 5) == 0;

    char name[FS_NAME_MAX];
    uint32_t size = 0;
    int listed = 0;
    for (int i = 0; fs_list(FS_DISK_PREFIX, i, name, &size, NULL) == 0; i++) {
        if (strcmp(name, "selftest.tmp") == 0 && size == sizeof(out)) {
            listed = 1;
        }
    }
    ok = ok && listed && fs_close(fd) == 0;

    fd = fs_open(path, FS_O_TRUNC);
    ok = ok && fd >= 0 && fs_size(fd) == 0 && fs_read(fd, 0, in, 10) == 0 && fs_close(fd) == 0 &&
         fs_unlink(path) == 0 && fs_open(path, 0) == -1;
    report("disk file system", ok, "ok");
    report("directories on disk", check_directories(FS_DISK_PREFIX), "ok");
}

static void test_disk_full(void) {
    uint64_t sectors = blk_capacity();
    if (sectors == 0 || name_lookup(FS_DISK_SERVICE_NAME) <= 0) {
        return;     // 没有磁盘：上面已经报告过 skipped
    }
    if (sectors > 4 * 2048) {
        // 要把整块盘写满；只在 make test 那样的小盘上做，不去折腾真正在用的磁盘
        printf("selftest: disk full: skipped (disk larger than 4 MB)\n");
        return;
    }

    static char chunk[16384];
    for (size_t i = 0; i < sizeof(chunk); i++) {
        chunk[i] = (char)i;
    }
    const char *fill = FS_DISK_PREFIX "selftest.fill";
    const char *other = FS_DISK_PREFIX "selftest.other";

    // 一直写到写不下：最后一次是部分写入或失败，之前写进去的都算数
    int fd = fs_open(fill, FS_O_CREATE | FS_O_TRUNC);
    uint32_t total = 0;
    long n = 0;
    while (fd >= 0 && (n = fs_write(fd, total, chunk, sizeof(chunk))) == (long)sizeof(chunk)) {
        total += (uint32_t)n;
    }
    if (n > 0) {
        total += (uint32_t)n;
    }
    uint64_t disk_bytes = sectors * BLK_SECTOR_SIZE;
    int ok = fd >= 0 && total > disk_bytes / 2 && total < disk_bytes && fs_size(fd) == (long)total &&
             fs_write(fd, total, chunk, 1) == -1;

    // 盘满时别的文件也写不进去；删掉大文件之后空间回来了
    int fd2 = fs_open(other, FS_O_CREATE | FS_O_TRUNC);
    ok = ok && fd2 >= 0 && fs_write(fd2, 0, chunk, 1) == -1 &&
         fs_unlink(fill) == 0 &&
         fs_write(fd2, 0, chunk, sizeof(chunk)) == (long)sizeof(chunk) &&
         fs_close(fd2) == 0 && fs_unlink(other) == 0;
    report("disk full", ok, "ok");
}

static void test_tcp(const struct net_info *info);

static void test_network(void) {
    // 网络服务与我们同时启动，给它一点时间登记；没有网卡时它会直接退出
    for (int i = 0; i < 25 && name_lookup(NET_SERVICE_NAME) == 0; i++) {
        usleep(20000);
    }
    struct net_info info;
    if (net_info(&info) != 0) {
        printf("selftest: network: skipped (no network device)\n");
        return;
    }

    // 地址由 DHCP 配置，最多等几秒（等不到时网络服务会退回固定地址）
    for (int i = 0; i < 250 && info.ip == 0; i++) {
        usleep(20000);
        net_info(&info);
    }
    report("network address configured", info.ip != 0 && info.gateway != 0 && info.netmask != 0, "ok");
    if (info.ip == 0) {
        return;
    }
    char ip[16];
    net_format_ip(info.ip, ip);
    printf("selftest: address %s (%s)\n", ip, info.dhcp ? "dhcp" : "static");

    // ping 网关（要经过网卡和 ARP）和自己（协议栈内部回环）；不存在的地址要超时
    uint32_t rtt = 0;
    uint64_t start = uptime_ms();
    int ok = net_ping(info.gateway, 1000, &rtt) == 0 && rtt < 1000 &&
             net_ping(info.ip, 1000, &rtt) == 0;
    report("ping gateway and self", ok, "ok");

    start = uptime_ms();
    ok = net_ping((info.ip & info.netmask) | 77, 300, &rtt) == -1 && uptime_ms() - start >= 250;
    report("ping to an unused address", ok, "refused");

    // UDP：两个套接字，经协议栈回环互发
    int a = net_udp_open(4000);
    int b = net_udp_open(0);
    static char out[1200], in[1500];
    for (size_t i = 0; i < sizeof(out); i++) {
        out[i] = (char)(i * 3 + 1);
    }
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    ok = a >= 0 && b >= 0 && a != b && net_udp_open(4000) == -1 &&
         net_udp_send(b, info.ip, 4000, out, sizeof(out)) == 0 &&
         net_udp_recv(a, in, sizeof(in), 500, &src_ip, &src_port) == (long)sizeof(out) &&
         memcmp(out, in, sizeof(out)) == 0 && src_ip == info.ip && src_port >= 49152 &&
         net_udp_send(a, info.ip, src_port, "pong", 4) == 0 &&
         net_udp_recv(b, in, sizeof(in), 500, NULL, NULL) == 4 && memcmp(in, "pong", 4) == 0;

    // 没有数据时：不等待立刻返回，等待则在超时后返回
    start = uptime_ms();
    ok = ok && net_udp_recv(a, in, sizeof(in), 0, NULL, NULL) == -1 &&
         net_udp_recv(a, in, sizeof(in), 200, NULL, NULL) == -1 && uptime_ms() - start >= 150 &&
         net_udp_close(a) == 0 && net_udp_close(b) == 0 &&
         net_udp_send(a, info.ip, 4000, out, 1) == -1;
    report("udp sockets", ok, "ok");

    // 分片和重组：让网络服务把每个包切成 256 字节一片，一个 1200 字节的数据报要分成 5 片发出、
    // 再拼回来；恢复正常之后不分片的包照常能过
    a = net_udp_open(4001);
    b = net_udp_open(0);
    memset(in, 0, sizeof(in));
    ok = a >= 0 && b >= 0 && net_debug_fragment(256) == 0 &&
         net_udp_send(b, info.ip, 4001, out, sizeof(out)) == 0 &&
         net_udp_recv(a, in, sizeof(in), 500, NULL, NULL) == (long)sizeof(out) &&
         memcmp(out, in, sizeof(out)) == 0;
    ok = net_debug_fragment(0) == 0 && ok;
    ok = ok && net_udp_send(b, info.ip, 4001, out, 100) == 0 &&
         net_udp_recv(a, in, sizeof(in), 500, NULL, NULL) == 100;
    net_udp_close(a);
    net_udp_close(b);
    report("ip fragmentation and reassembly", ok, "ok");

    // 续租：让网络服务现在就向 DHCP 服务器续租，成功的次数要增加，地址不变
    if (!info.dhcp) {
        printf("selftest: dhcp lease renewal: skipped (address is not from DHCP)\n");
    } else {
        long before = net_debug_renew();
        long after = before;
        start = uptime_ms();
        while (before >= 0 && after == before && uptime_ms() - start < 3000) {
            usleep(50000);
            after = net_debug_renew();
        }
        struct net_info renewed;
        ok = before >= 0 && after > before && net_info(&renewed) == 0 && renewed.ip == info.ip && renewed.dhcp;
        report("dhcp lease renewal", ok, "ok");
    }

    test_tcp(&info);
}

/** 通过连接 conn 发 len 字节的图案（由 seed 决定），再原样读回来 */
static bool tcp_echo_round(int conn, size_t len, int seed, uint32_t recv_timeout_ms) {
    static char tx[8000], rx[8000];
    for (size_t i = 0; i < len; i++) {
        tx[i] = (char)(seed * 31 + i * 7 + i / 255);
    }
    if (net_tcp_send(conn, tx, len) != (long)len) {
        return false;
    }
    size_t got = 0;
    while (got < len) {
        long n = net_tcp_recv(conn, rx + got, len - got, recv_timeout_ms);
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    return memcmp(tx, rx, len) == 0;
}

static void test_tcp_listen(const struct net_info *info) {
    // 监听：服务端和客户端都在本机，段在协议栈内部回环
    int listener = net_tcp_listen(8080);
    int ok = listener >= 0 && net_tcp_listen(8080) == -1 &&          // 端口已被占用
             net_tcp_accept(listener, 0, NULL, NULL) == -1;          // 还没有人连进来

    static char big[20000];
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (char)(i * 5 + i / 251);
    }

    int child = fork();
    if (child == 0) {
        // 客户端：问候，收应答，再发一大块数据，最后读到对方关闭
        char reply[32];
        int conn = net_tcp_connect(info->ip, 8080, 3000);
        int good = conn >= 0 &&
                   net_tcp_send(conn, "hello server", 12) == 12 &&
                   net_tcp_recv(conn, reply, sizeof(reply), 3000) == 12 &&
                   memcmp(reply, "hello client", 12) == 0 &&
                   net_tcp_send(conn, big, sizeof(big)) == (long)sizeof(big) &&
                   net_tcp_recv(conn, reply, sizeof(reply), 5000) == 0 &&   // 服务端关了
                   net_tcp_close(conn) == 0;
        exit(good ? 0 : 1);
    }

    uint32_t peer_ip = 0;
    uint16_t peer_port = 0;
    char greeting[32];
    static char received[20000];
    int conn = net_tcp_accept(listener, 3000, &peer_ip, &peer_port);
    ok = ok && conn >= 0 && peer_ip == info->ip && peer_port >= 49152 &&
         net_tcp_recv(conn, greeting, sizeof(greeting), 3000) == 12 &&
         memcmp(greeting, "hello server", 12) == 0 &&
         net_tcp_send(conn, "hello client", 12) == 12;
    size_t got = 0;
    while (ok && got < sizeof(received)) {
        long n = net_tcp_recv(conn, received + got, sizeof(received) - got, 5000);
        if (n <= 0) {
            ok = 0;
            break;
        }
        got += (size_t)n;
    }
    ok = ok && memcmp(big, received, sizeof(big)) == 0 && net_tcp_close(conn) == 0;

    int status = 1;
    waitpid(child, &status, 0);
    ok = ok && WEXITSTATUS(status) == 0;

    // 关掉监听之后，再连这个端口会被拒绝
    uint64_t start = uptime_ms();
    ok = ok && net_tcp_close(listener) == 0 &&
         net_tcp_connect(info->ip, 8080, 2000) == -1 && uptime_ms() - start < 1500;
    report("tcp listen and accept over loopback", ok, "ok");
}

static void test_tcp(const struct net_info *info) {
    uint64_t start;
    int ok;

    test_tcp_listen(info);

    // TCP：连到一个没人监听的端口会被拒绝（网关把它转给宿主机的 127.0.0.1:1）
    start = uptime_ms();
    ok = net_tcp_connect(info->gateway, 1, 3000) == -1 && uptime_ms() - start < 2500;
    report("tcp connect to a closed port", ok, "refused");

    // 回显服务：make run / make test 用 QEMU 的 guestfwd 把 10.0.2.100:7 接到宿主机的 cat 上
    const uint32_t echo_ip = NET_IP(10, 0, 2, 100);
    int conn = net_tcp_connect(echo_ip, 7, 1500);
    if (conn < 0) {
        printf("selftest: tcp echo: skipped (no echo service at 10.0.2.100:7)\n");
        return;
    }

    // 一次发 5000 字节（要拆成多个段），再原样读回来
    static char tx[5000], rx[5000];
    for (size_t i = 0; i < sizeof(tx); i++) {
        tx[i] = (char)(i * 7 + i / 255);
    }
    ok = net_tcp_send(conn, tx, sizeof(tx)) == (long)sizeof(tx);
    size_t got = 0;
    while (ok && got < sizeof(rx)) {
        long n = net_tcp_recv(conn, rx + got, sizeof(rx) - got, 3000);
        if (n <= 0) {
            ok = 0;
            break;
        }
        got += (size_t)n;
    }
    ok = ok && memcmp(tx, rx, sizeof(tx)) == 0;

    // 再来回 20 轮、每轮 1000 字节：总量超过两个方向的缓冲区，序号和环形缓冲区都要绕回去
    for (int round = 0; ok && round < 20; round++) {
        for (int i = 0; i < 1000; i++) {
            tx[i] = (char)(round * 31 + i);
        }
        ok = net_tcp_send(conn, tx, 1000) == 1000;
        got = 0;
        while (ok && got < 1000) {
            long n = net_tcp_recv(conn, rx + got, 1000 - got, 3000);
            if (n <= 0) {
                ok = 0;
                break;
            }
            got += (size_t)n;
        }
        ok = ok && memcmp(tx, rx, 1000) == 0;
    }

    // 没有数据时 recv 按时超时；关闭之后这个连接号不能再用
    start = uptime_ms();
    ok = ok && net_tcp_recv(conn, rx, 10, 200) == -1 && uptime_ms() - start >= 150 &&
         net_tcp_close(conn) == 0 && net_tcp_send(conn, tx, 1) == -1;
    report("tcp echo", ok, "ok");

    // 重传：让网络服务丢掉指定个数的 TCP 帧，连接必须靠重传恢复
    long before = net_debug_drop(1, 0);                 // 丢掉我们的 SYN
    start = uptime_ms();
    conn = net_tcp_connect(echo_ip, 7, 5000);
    uint64_t connect_ms = uptime_ms() - start;
    int step = 0;                                       // 失败时指出是哪一步
    ok = before >= 0 && conn >= 0 && connect_ms >= 250; // 等了一个重传超时
    step += ok;
    net_debug_drop(1, 0);                               // 丢一个数据段
    ok = ok && tcp_echo_round(conn, 1000, 1, 8000);
    step += ok;
    net_debug_drop(3, 0);                               // 一次发的三个段全丢
    ok = ok && tcp_echo_round(conn, 4000, 2, 8000);
    step += ok;
    net_debug_drop(0, 1);                               // 丢一个收到的帧：对方的数据或确认
    ok = ok && tcp_echo_round(conn, 1000, 3, 8000);
    step += ok;
    long after = net_debug_drop(0, 0);
    ok = ok && after - before >= 3;                     // 至少 SYN、一个段、一批段各重传一次
    if (!ok) {
        printf("selftest: (retransmission stopped at step %d: conn %d, connect took %u ms, drops %ld -> %ld)\n",
               step, conn, (unsigned)connect_ms, before, after);
    }
    report("tcp retransmission after lost frames", ok, "ok");

    // 分片的包经过真的网卡：每个 TCP 段被切成几片发出去，对方要拼得回来
    net_debug_fragment(512);
    ok = conn >= 0 && tcp_echo_round(conn, 3000, 4, 8000);
    net_debug_fragment(0);
    report("tcp over fragmented packets", ok, "ok");

    // 接收窗口：先发 12000 字节而不去读，回显的数据填满我们 8KB 的接收缓冲区，
    // 窗口关闭；然后开始读，窗口重新打开，剩下的数据要能接着到
    static char wtx[12000], wrx[12000];
    for (size_t i = 0; i < sizeof(wtx); i++) {
        wtx[i] = (char)(i * 13 + i / 199);
    }
    ok = conn >= 0 && net_tcp_send(conn, wtx, sizeof(wtx)) == (long)sizeof(wtx);
    usleep(300000);
    got = 0;
    while (ok && got < sizeof(wrx)) {
        long n = net_tcp_recv(conn, wrx + got, sizeof(wrx) - got, 5000);
        if (n <= 0) {
            ok = 0;
            break;
        }
        got += (size_t)n;
    }
    ok = ok && memcmp(wtx, wrx, sizeof(wtx)) == 0 && net_tcp_close(conn) == 0;
    report("tcp receive window closes and reopens", ok, "ok");

    // 服务崩溃了 init 会重启它：让网络服务退出（像崩溃一样），它要以另一个进程的身份
    // 重新出现，重新拿到网卡、配好地址，原来的客户（我们）不用做任何事就能接着用
    int old_net = name_lookup(NET_SERVICE_NAME);
    ok = old_net > 0 && net_debug_exit() == 0;
    int new_net = 0;
    start = uptime_ms();
    while (ok && uptime_ms() - start < 5000) {
        new_net = name_lookup(NET_SERVICE_NAME);
        if (new_net > 0 && new_net != old_net) {
            break;
        }
        usleep(20000);
    }
    struct net_info again = {};
    while (ok && uptime_ms() - start < 10000 && (net_info(&again) != 0 || again.ip == 0)) {
        usleep(20000);
    }
    uint32_t rtt = 0;
    ok = ok && new_net > 0 && new_net != old_net && again.ip == info->ip &&
         net_ping(again.gateway, 1000, &rtt) == 0;
    if (!ok) {
        printf("selftest: (net was pid %d, is pid %d, address %u)\n", old_net, new_net, again.ip);
    }
    report("a service that exits is restarted", ok, "ok");
}
// 反复创建并结束进程，走遍几条退出路径
static bool churn_processes(int rounds, const void *image, size_t image_size) {
    bool ok = true;
    for (int r = 0; ok && r < rounds; r++) {
        int status = 0;

        // 1. 用掉一些内存（匿名映射、堆、写时复制的页）然后正常退出
        int pid = fork();
        if (pid == 0) {
            char *mem = (char *)mmap(NULL, 64 * 4096, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED) {
                exit(1);
            }
            for (int i = 0; i < 64; i++) {
                mem[i * 4096] = (char)i;
            }
            munmap(mem, 16 * 4096);         // 一部分自己还，其余留给退出时回收
            exit(0);
        }
        ok = ok && pid > 0 && waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 0;

        // 2. 换成另一个程序再退出
        pid = fork();
        if (pid == 0) {
            const char *argv[] = { "sleep", "0", NULL };
            exec(image, image_size, argv);
            exit(1);
        }
        ok = ok && pid > 0 && waitpid(pid, &status, 0) == pid && WEXITSTATUS(status) == 0;

        // 3. 阻塞在 IPC 里时被杀掉
        pid = fork();
        if (pid == 0) {
            struct ipc_msg m;
            for (;;) {
                ipc_recv(IPC_ANY, &m);
            }
        }
        usleep(10000);
        ok = ok && pid > 0 && kill(pid, 9) == 0 && waitpid(pid, &status, 0) == pid && WIFSIGNALED(status);
    }
    return ok;
}

// 进程结束后，它用过的物理内存（页、页表、内核栈）要全部归还
static void test_memory_reclaimed(void) {
    int fd = fs_open("sleep", 0);
    long size = fd >= 0 ? fs_size(fd) : -1;
    void *image = size > 0 ? mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) : MAP_FAILED;
    bool ok = image != MAP_FAILED && fs_read(fd, 0, image, (size_t)size) == size;
    if (fd >= 0) {
        fs_close(fd);
    }

    // 先跑两轮：内核堆之类只增不减的东西长到位，之后的数字才可比
    ok = ok && churn_processes(2, image, (size_t)size);
    long before = mem_free_pages();
    ok = ok && churn_processes(10, image, (size_t)size);
    long after = mem_free_pages();

    if (ok && after != before) {
        printf("selftest: (free pages %ld -> %ld after 30 processes)\n", before, after);
    }
    report("memory of exited processes is reclaimed", ok && after == before, "ok");
    if (image != MAP_FAILED) {
        munmap(image, (size_t)size);
    }
}

// selftest restart <名字>：让终端输入的一个模块（console、uart、kbd）像崩溃了一样退出，
// 等 init 把它重启。重启之后输入还通不通要有人敲键盘才知道：那是 scripts/shell-test.sh 的事
static int restart_module(const char *name) {
    int old_pid = name_lookup(name);
    if (old_pid <= 0 || console_debug_exit(name) != 0) {
        eprintf("selftest: no module registered as %s\n", name);
        return 1;
    }
    uint64_t start = uptime_ms();
    while (uptime_ms() - start < 5000) {
        int new_pid = name_lookup(name);
        if (new_pid > 0 && new_pid != old_pid) {
            printf("selftest: %s restarted (pid %d -> %d)\n", name, old_pid, new_pid);
            return 0;
        }
        usleep(20000);
    }
    printf("selftest: %s was NOT restarted\n", name);
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "restart") == 0) {
        return restart_module(argv[2]);
    }
    // 命令行（或 rc）是带着程序名启动我们的
    report("program arguments", argc >= 1 && strcmp(argv[0], "selftest") == 0 && argv[argc] == NULL, "ok");

    test_memory_and_fork();
    test_memory_reclaimed();
    test_ipc();
    test_ipc_blocking();
    test_timer();
    test_privilege();
    test_floating_point();
    test_cpus();
    test_shared_memory();
    test_parallel_memory();
    test_parallel_ipc();
    test_names();
    test_fs();
    // 启动映像里的子目录（user/bootfs/docs）装载出来也是目录
    report("directories", check_directories("") && dir_has("", "docs", true) && dir_has("docs", "paths.txt", false),
           "ok");
    test_fs_client_reclaim();
    test_block_device();
    test_disk_fs();
    test_disk_full();
    test_network();

    if (failures == 0) {
        printf("selftest: all passed\n");
    } else {
        printf("selftest: %d FAILED\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
