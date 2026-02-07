# TMCL 架构

## 模块结构

```
src/
├── main.c              # 入口 + 页面导航 (T/V/C/A 标签页)
├── home/
│   ├── home.h          # HomeState (pinned_index, last_index, selected)
│   └── home.c          # 首页: 固定版本 + 最近版本快速启动
├── version/
│   ├── version.h       # VersionInfo, VersionState
│   ├── version.c       # 版本列表、启动、重命名、删除
│   └── install.c       # 版本安装 (manifest → 下载 → assets)
├── config/
│   ├── config.h        # ConfigItem, ConfigState
│   └── config.c        # ~/.tmcl/tmcl.json 读写、配置 UI
├── account/
│   ├── account.h       # AccountInfo, AccountState
│   └── account.c       # 账户管理 (待开发)
└── job/
    ├── job.h           # (预留)
    └── job.c           # (预留)
```

## 数据流

```
main.c
  ├─ initscr() → 主循环
  │   ├─ home_page()    ← HomeState
  │   ├─ version_page() ← VersionState
  │   ├─ config_page()  ← ConfigState
  │   └─ account_page() ← AccountState
  │
  ├─ begin_version()
  │   ├─ ConfigState → java_path, memory, game_dir
  │   ├─ VersionState → mainClass, inheritsFrom, javaMajor
  │   ├─ classpath_from_json() → 构建 classpath
  │   └─ fork() + execvp(java, args)
  │
  └─ install_page()
      ├─ fetch_versions() → version_manifest_v2.json
      ├─ do_install()
      │   ├─ dl_parallel() → curl 多线程下载
      │   └─ retry_failed() → 重试 2 次
      └─ init_versions() → 刷新版本列表
```

## 配置存储

- `~/.tmcl/tmcl.json` — 启动器配置 (java_path, memory, game_dir, ...)
- `~/.tmcl/accounts.json` — 账户列表 (待实现)

## 版本 JSON 结构

Minecraft 版本目录 `~/.minecraft/versions/<name>/`:
- `<name>.json` — 版本元数据 (mainClass, arguments, libraries, assetIndex, inheritsFrom)
- `<name>.jar` — 游戏 JAR

Forge/Fabric 通过 `inheritsFrom` 引用原版父版本。
