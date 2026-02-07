# TMCL — Terminal Minecraft Launcher

基于 C 语言 + ncurses 的 TUI Minecraft Java 版启动器，优先支持 Linux。

## 快速开始

```bash
make clean && make    # 编译
./tmcl                # 运行
make install          # 安装到 /usr/local/bin
```

依赖: `gcc`, `ncurses`, `cjson`, `curl`, `pthread`

## 关键文件索引

| 路径 | 说明 |
|------|------|
| `src/main.c` | 入口 + 页面导航 |
| `src/home/home.c` | 首页（固定/最近版本快速启动） |
| `src/version/version.c` | 版本管理（列表、启动、安装） |
| `src/version/install.c` | 版本安装（下载 manifest、libraries、assets） |
| `src/version/version.h` | VersionInfo / VersionState 结构 |
| `src/config/config.c` | 配置管理（读取/写入 ~/.tmcl/tmcl.json） |
| `src/config/config.h` | ConfigItem / ConfigState 结构 |
| `src/account/account.c` | 账户管理（待开发） |
| `src/account/account.h` | AccountInfo / AccountState 结构 |
| `devlog/TODO.md` | 当前开发待办 |
| `devlog/CHANGELOG.md` | 开发日志 |
| `docs/architecture.md` | 代码架构说明 |
| `docs/coding-style.md` | 编码规范 |
| `docs/requirements.md` | 功能需求文档 |
| `docs/specs/` | 各功能的详细规格说明 |

## 开发流程

1. **讨论** — 用中文与用户讨论需求、优先级、方案
2. **写计划** — 将计划写入项目根目录的 `CLAUDE.md` 或 `devlog/TODO.md`，不要用 `~/.claude/plans/`
3. **实现** — 按计划逐步修改代码，每步编译验证
4. **编译验证** — `make clean && make` 必须 0 error
5. **记录** — 将完成的工作写入 `devlog/CHANGELOG.md`

## 代码风格

- 注释使用**中文**
- 函数名 `snake_case`，结构体 `PascalCase`，宏 `UPPER_SNAKE`
- 缩进 2 空格
- 严格检查缓冲区溢出（`snprintf` + `sizeof`）
- 所有 `malloc` 必须对应 `free`

## 注意事项

1. **ncurses 生命周期**: `begin_version()` 和 `do_install()` 调用 `endwin()` 后，主循环必须用 `isendwin()` 检测并重启 ncurses
2. **缓冲区大小**: 版本名/路径最长 256，classpath 最长 16384，参数数组 300 个槽位
3. **Maven 路径**: groupId 中的 `.` 必须转为 `/`（如 `net.minecraft` → `net/minecraft`）
4. **信号处理**: 安装下载使用 `sigaction` + `fork`/`exec`，避免 `system()` 阻塞 SIGINT
5. **多线程**: 安装时 library 用 8 线程，assets 用配置中 `threads` 值（默认 96）

## 当前版本状态

- ✅ 原版 Minecraft 启动
- ✅ Forge 1.7.10+ 启动
- ✅ Fabric 1.21+ 启动
- ✅ 从云端安装原版版本
- ✅ 多线程下载 + 断点重试
- ✅ 自动内存分配（`auto`）
- ⬜ 账户系统（下一阶段开发目标）
