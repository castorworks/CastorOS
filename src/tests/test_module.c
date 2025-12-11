// ============================================================================
// test_module.c - 测试模块注册表实现
// ============================================================================
//
// 实现测试模块的注册、查找和运行功能
//
// **Feature: test-refactor**
// **Validates: Requirements 10.2, 12.2, 12.3, 13.1**
// ============================================================================

#include <tests/test_module.h>
#include <tests/ktest.h>
#include <tests/test_runner.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// ============================================================================
// 注册表操作实现
// ============================================================================

/**
 * @brief 初始化测试模块注册表
 */
void test_registry_init(test_registry_t *registry) {
    if (registry == NULL) {
        return;
    }
    registry->count = 0;
    for (uint32_t i = 0; i < TEST_MODULE_MAX_COUNT; i++) {
        registry->modules[i] = NULL;
    }
}

/**
 * @brief 注册一个测试模块
 */
bool test_registry_add(test_registry_t *registry, const test_module_t *module) {
    if (registry == NULL || module == NULL) {
        return false;
    }
    if (registry->count >= TEST_MODULE_MAX_COUNT) {
        kprintf("Warning: Test registry full, cannot add module '%s'\n", 
                module->name);
        return false;
    }
    
    // 检查是否已存在同名模块
    for (uint32_t i = 0; i < registry->count; i++) {
        if (registry->modules[i] != NULL &&
            strcmp(registry->modules[i]->name, module->name) == 0) {
            kprintf("Warning: Module '%s' already registered\n", module->name);
            return false;
        }
    }
    
    registry->modules[registry->count++] = module;
    return true;
}

/**
 * @brief 按名称查找模块
 */
const test_module_t* test_registry_find(const test_registry_t *registry, 
                                         const char *name) {
    if (registry == NULL || name == NULL) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < registry->count; i++) {
        if (registry->modules[i] != NULL &&
            strcmp(registry->modules[i]->name, name) == 0) {
            return registry->modules[i];
        }
    }
    return NULL;
}

/**
 * @brief 获取指定子系统的所有模块
 */
uint32_t test_registry_get_by_subsystem(const test_registry_t *registry,
                                         test_subsystem_t subsystem,
                                         const test_module_t **out_modules,
                                         uint32_t max_count) {
    if (registry == NULL || out_modules == NULL || max_count == 0) {
        return 0;
    }
    
    uint32_t found = 0;
    for (uint32_t i = 0; i < registry->count && found < max_count; i++) {
        if (registry->modules[i] != NULL &&
            registry->modules[i]->subsystem == subsystem) {
            out_modules[found++] = registry->modules[i];
        }
    }
    return found;
}

/**
 * @brief 从字符串解析子系统枚举
 */
test_subsystem_t test_subsystem_from_string(const char *name) {
    if (name == NULL) {
        return TEST_SUBSYSTEM_COUNT;
    }
    
    for (uint32_t i = 0; i < TEST_SUBSYSTEM_COUNT; i++) {
        if (strcmp(TEST_SUBSYSTEM_NAMES[i], name) == 0) {
            return (test_subsystem_t)i;
        }
    }
    return TEST_SUBSYSTEM_COUNT;
}

// ============================================================================
// 模块化运行函数实现
// ============================================================================

/**
 * @brief 检查模块依赖是否满足
 */
static bool check_dependencies(const test_registry_t *registry,
                               const test_module_t *module,
                               bool *executed,
                               uint32_t module_index) {
    (void)module_index;  // 未来可用于循环依赖检测
    
    if (module->dep_count == 0 || module->dependencies == NULL) {
        return true;
    }
    
    for (uint32_t i = 0; i < module->dep_count; i++) {
        const char *dep_name = module->dependencies[i];
        bool found = false;
        
        for (uint32_t j = 0; j < registry->count; j++) {
            if (registry->modules[j] != NULL &&
                strcmp(registry->modules[j]->name, dep_name) == 0) {
                found = true;
                if (!executed[j]) {
                    // 依赖模块尚未执行
                    return false;
                }
                break;
            }
        }
        
        if (!found) {
            kprintf("Warning: Module '%s' depends on unknown module '%s'\n",
                    module->name, dep_name);
            return false;
        }
    }
    return true;
}

/**
 * @brief 运行单个测试模块
 */
static void run_single_module(const test_module_t *module, 
                              const test_run_options_t *options) {
    if (!test_module_should_run(module)) {
        if (options->verbose) {
            kprintf("  [SKIP] %s (architecture not supported)\n", module->name);
        }
        return;
    }
    
    if (module->is_slow && !options->include_slow) {
        if (options->verbose) {
            kprintf("  [SKIP] %s (slow test)\n", module->name);
        }
        return;
    }
    
    // 运行模块测试
    if (options->verbose) {
        kprintf("\n[Module: %s] %s\n", module->name, module->description);
        kprintf("  Subsystem: %s\n", test_subsystem_name(module->subsystem));
        if (module->dep_count > 0) {
            kprintf("  Dependencies: ");
            for (uint32_t i = 0; i < module->dep_count; i++) {
                kprintf("%s%s", module->dependencies[i],
                        i < module->dep_count - 1 ? ", " : "\n");
            }
        }
    }
    
    module->run_func();
}

/**
 * @brief 使用选项运行测试
 */
void test_run_with_options(const test_registry_t *registry,
                           const test_run_options_t *options) {
    if (registry == NULL || options == NULL) {
        return;
    }
    
    const arch_info_t *arch = test_get_arch_info();
    
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| CastorOS Modular Test Suite\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    // 打印架构信息
    kprintf("\nTarget Architecture: %s (%u-bit)\n", arch->name, arch->bits);
    kprintf("Registered modules: %u\n", registry->count);
    
    // 打印过滤条件
    if (options->filter_module != NULL) {
        kprintf("Filter: module = '%s'\n", options->filter_module);
    }
    if (options->filter_subsystem != NULL) {
        kprintf("Filter: subsystem = '%s'\n", options->filter_subsystem);
    }
    if (options->include_slow) {
        kprintf("Including slow tests\n");
    }
    
    kprintf("\n");
    
    // 跟踪已执行的模块（用于依赖检查）
    bool executed[TEST_MODULE_MAX_COUNT] = {false};
    uint32_t executed_count = 0;
    uint32_t skipped_count = 0;
    
    // 解析子系统过滤
    test_subsystem_t filter_subsys = TEST_SUBSYSTEM_COUNT;
    if (options->filter_subsystem != NULL) {
        filter_subsys = test_subsystem_from_string(options->filter_subsystem);
        if (filter_subsys == TEST_SUBSYSTEM_COUNT) {
            kprintf("Warning: Unknown subsystem '%s'\n", options->filter_subsystem);
        }
    }
    
    // 多轮执行以处理依赖
    bool progress = true;
    while (progress) {
        progress = false;
        
        for (uint32_t i = 0; i < registry->count; i++) {
            if (executed[i]) {
                continue;
            }
            
            const test_module_t *module = registry->modules[i];
            if (module == NULL) {
                continue;
            }
            
            // 应用过滤条件
            if (options->filter_module != NULL &&
                strcmp(module->name, options->filter_module) != 0) {
                continue;
            }
            
            if (filter_subsys != TEST_SUBSYSTEM_COUNT &&
                module->subsystem != filter_subsys) {
                continue;
            }
            
            // 检查依赖
            if (!check_dependencies(registry, module, executed, i)) {
                continue;
            }
            
            // 检查架构兼容性
            if (!test_module_should_run(module)) {
                executed[i] = true;
                skipped_count++;
                if (options->verbose) {
                    kprintf("[SKIP] %s (architecture not supported)\n", 
                            module->name);
                }
                progress = true;
                continue;
            }
            
            // 检查慢测试
            if (module->is_slow && !options->include_slow) {
                executed[i] = true;
                skipped_count++;
                if (options->verbose) {
                    kprintf("[SKIP] %s (slow test)\n", module->name);
                }
                progress = true;
                continue;
            }
            
            // 运行模块
            kprintf("[Module %u/%u] %s\n", 
                    executed_count + 1, registry->count, module->name);
            
            run_single_module(module, options);
            
            executed[i] = true;
            executed_count++;
            progress = true;
            
            // 检查是否需要停止
            test_stats_t stats = unittest_get_stats();
            if (options->stop_on_failure && stats.failed > 0) {
                kprintf("\nStopping on first failure\n");
                goto done;
            }
        }
    }
    
done:
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("|| Modules executed: %u, Skipped: %u\n", executed_count, skipped_count);
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

/**
 * @brief 运行指定子系统的所有测试
 */
void test_run_subsystem(const test_registry_t *registry, const char *subsystem) {
    test_run_options_t options = TEST_RUN_OPTIONS_DEFAULT;
    options.filter_subsystem = subsystem;
    options.verbose = true;
    test_run_with_options(registry, &options);
}

/**
 * @brief 运行指定模块的测试
 */
void test_run_module(const test_registry_t *registry, const char *module_name) {
    test_run_options_t options = TEST_RUN_OPTIONS_DEFAULT;
    options.filter_module = module_name;
    options.verbose = true;
    test_run_with_options(registry, &options);
}
