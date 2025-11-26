# CastorOS

> CastorOS is an operating system designed for learning and fun.

## 构建系统

+ 参考 [docs/00-environment.md](./docs/00-environment.md) 安装开发环境

+ 编译并运行

  ```bash
  make clean
  make run
  ```

## 当前进展

+ [x] [开发环境搭建](./docs/00-environment.md)
+ [x] [系统引导](./docs/01-boot.md)
+ [x] [基础设施](./docs/02-infrastructure.md)
+ [x] [内存管理](./docs/03-mm.md)
+ [x] [补充驱动](./docs/04-drivers.md)
+ [x] [任务管理](./docs/05-task.md)
+ [x] [内核 Shell](./docs/06-kernel-shell.md)
+ [x] [文件系统](./docs/07-fs.md)
+ [x] [FAT32 文件系统](./docs/08-fat32.md)
+ [x] [用户模式](./docs/09-usermode.md)
+ [x] [同步机制](./docs/10-sync.md)

## Git 提交格式

+ `feat` 添加了新特性
+ `fix` 修复问题
+ `style` 无逻辑改动的代码风格调整
+ `perf` 性能/优化
+ `refactor` 重构
+ `revert` 回滚提交
+ `test` 测试
+ `docs` 文档
+ `chore` 依赖或者脚手架调整
+ `workflow` 工作流优化
+ `ci` 持续集成
+ `types` 类型定义
+ `wip` 开发中
