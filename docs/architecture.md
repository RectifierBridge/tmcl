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

## 键盘导航

### 页面切换

| 键 | 功能 |
|----|------|
| `T` | 切换到首页 |
| `V` | 切换到版本页面 |
| `C` | 切换到配置页面 |
| `A` | 切换到账户页面 |
| `L` / `→` (右方向键) | 切换至下一个标签页 |
| `H` / `←` (左方向键) | 切换至上一个标签页 |
| `q` | 退出程序 |

### 列表/光标导航

所有列表和选择器统一支持两套导航键：

| 键 | 方向键等效 | 功能 |
|----|-----------|------|
| `j` | `↓` | 光标下移 |
| `k` | `↑` | 光标上移 |
| `h` | `←` | 操作间左移 / 选项左移 |
| `l` | `→` | 操作间右移 / 选项右移 |

适用范围：
- **首页** (Home): `j` / `k` / `↓` / `↑` 选择版本
- **版本页** (Version): `j` / `k` / `↓` / `↑` 选择版本；`h` / `l` / `←` / `→` 在底部操作间切换
- **配置页** (Config): `j` / `k` / `↓` / `↑` 选择配置项；`h` / `l` / `←` / `→` 在底部操作间切换
- **账户页** (Account): `j` / `k` / `↓` / `↑` 选择账户；`h` / `l` / `←` / `→` 在底部操作间切换
- **安装页** (Install): 主表单及所有子选择器（类型、版本、模组加载器、加载器版本）均支持 `j` / `k` / `↓` / `↑` 导航

### 版本页操作键

| 键 | 功能 |
|----|------|
| `b` / `Enter` (光标在 Begin) | 启动游戏 |
| `p` / `Enter` (光标在 Pin) | 固定/取消固定到首页 |
| `m` / `Enter` (光标在 Mod) | 打开模组管理（预留） |
| `c` / `Enter` (光标在 Config) | 打开版本独立配置 |
| `n` / `Enter` (光标在 New) | 安装新版本 |
| `r` | 重命名版本 |
| `d` | 删除版本 |
