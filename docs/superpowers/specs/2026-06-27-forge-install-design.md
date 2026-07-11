# Forge/NeoForge 版本安装设计

日期: 2026-06-27
目标: 在 TMCL 中实现 Forge 1.13+ 和 NeoForge 的版本安装

## 概述

采用纯手动方案（方案 A：顺序流水线），自行解析 Forge installer、下载 libraries、执行 processors、生成自包含 version JSON。Forge 和 NeoForge 共享同一套代码路径，仅通过配置结构体区分 Maven 仓库和坐标。

## 覆盖范围

- Forge 1.13+（使用 modlauncher/installer profile 体系的版本）
- NeoForge 1.20.1+（从 Forge 分叉后的所有版本）
- 不覆盖 Forge 1.7.10-1.12.2（旧版 launchwrapper 体系，以后处理）

## 安装流程

```
用户选择 MC 版本 (在 install_page 中已完成)
        │
        ▼
用户选择 modloader: forge / neoforge
        │
        ▼
┌─────────────────────────────┐
│ 1. 拉取 Maven metadata       │
│    解析版本列表               │
│    过滤该 MC 版本对应的       │
│    所有 forge/neoforge 版本  │
│    用户选择目标版本           │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│ 2. 下载 installer jar        │
│    提取 install_profile.json  │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│ 3. 下载所有 libraries        │
│    · profile 顶层 libraries   │
│    · versionInfo libraries    │
│    · vanilla libraries         │
│    · vanilla client.jar        │
│    · asset index               │
│    (复用 dl_parallel + retry)  │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│ 4. 顺序执行 processors       │
│    for each processor:       │
│      提取 mainClass           │
│      构建 classpath           │
│      解析 ${data} 占位符      │
│      fork/exec java           │
│      检查退出码               │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│ 5. 生成自包含 version JSON   │
│    · 以 vanilla JSON 为基础   │
│    · 覆盖 mainClass           │
│    · 合并 arguments           │
│    · 合并 libraries（去重）   │
│    · 添加 forge 元数据        │
│    · 移除 inheritsFrom        │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│ 6. 下载 assets               │
│    (复用现有流程)             │
└─────────────────────────────┘
```

## 详细设计

### 1. Forge/NeoForge 统一配置结构

```c
typedef struct {
  const char *maven_base;    // "maven.minecraftforge.net" 或 "maven.neoforged.net"
  const char *group_id;      // "net.minecraftforge" 或 "net.neoforged"
  const char *artifact_id;   // "forge" 或 "neoforge"
  int has_promotions;        // forge=1, neoforge=0
} ForgeConfig;
```

`do_forge_install()` 接收 `ForgeConfig`，Forge 和 NeoForge 的其余流程完全相同。

### 2. 版本发现

**数据来源:**
- Maven metadata: `https://<maven_base>/net/minecraftforge/<artifact_id>/maven-metadata.xml`
  - 包含所有 `<version>` 条目，格式为 `<mc_version>-<forge_version>`（如 `1.20.1-47.2.0`）
- Forge 额外有 promotions API: `https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json`
  - 提供每个 MC 版本的 "recommended" 和 "latest" 标记

**UI 交互:**
- 用户在 install_page 中已选择了 MC 版本（selected_version）
- 选择 forge/neoforge 后，拉取 maven metadata
- 解析 XML，过滤出以 `<mc_version>-` 开头的条目
- 弹出版本选择器（ncurses UI），显示每个 version 及其推荐/最新标记
- 用户选择后进入安装

**新增函数:**
- `parse_maven_xml()` — 解析 maven-metadata.xml，提取版本列表
- `pick_forge_version()` — ncurses 版本选择器 UI

### 3. 下载 Installer Jar 并提取 install_profile.json

**Installer jar 位置:**
- URL: `https://<maven_base>/net/minecraftforge/<artifact_id>/<full_version>/<artifact_id>-<full_version>-installer.jar`
- 例如: `https://maven.minecraftforge.net/net/minecraftforge/forge/1.20.1-47.2.0/forge-1.20.1-47.2.0-installer.jar`

**提取:**
- 下载到临时路径
- 使用 `unzip -p <installer_jar> install_profile.json` 提取到内存
- cJSON 解析 install_profile.json

### 4. install_profile.json 结构

```json
{
  "versionInfo": {
    "id": "1.20.1-forge-47.2.0",
    "inheritsFrom": "1.20.1",
    "mainClass": "cpw.mods.modlauncher.Launcher",
    "arguments": {
      "game": [...],
      "jvm": ["-Dforge.enabled=true", ...]
    },
    "libraries": [
      {"name": "net.minecraftforge:forge:1.20.1-47.2.0", ...},
      ...
    ]
  },
  "libraries": [
    {"name": "net.minecraftforge:installertools:1.3.0", ...},
    {"name": "net.minecraftforge:jarsplitter:1.1.4", ...},
    ...
  ],
  "processors": [
    {
      "sides": ["client"],
      "jar": "net.minecraftforge:jarsplitter:1.1.4",
      "classpath": [
        "net.minecraftforge:jarsplitter:1.1.4",
        "net.minecraftforge:forge:1.20.1-47.2.0:installer"
      ],
      "args": [
        "--input", "{MINECRAFT_JAR}",
        "--output", "{SIDE_STRIPPER_OUTPUT}",
        "--side", "client"
      ],
      "outputs": {
        "{SIDE_STRIPPER_OUTPUT}": "binpatches.pack"
      }
    }
  ],
  "data": {
    "MINECRAFT_JAR": {
      "client": "[net.minecraft:minecraft:1.20.1:client]",
      "server": "[net.minecraft:minecraft:1.20.1:server]"
    },
    "BINPATCH": {
      "client": "[net.minecraftforge:forge:1.20.1-47.2.0:clientdata@lzma]",
      "server": "[net.minecraftforge:forge:1.20.1-47.2.0:serverdata@lzma]"
    },
    "ROOT": {
      "client": "<version_dir>",
      "server": "<version_dir>"
    },
    "SIDE": {
      "client": "client",
      "server": "server"
    }
  }
}
```

**关键字段说明:**
- `versionInfo` — 用作最终 version JSON 的基础结构
- 顶层 `libraries` — 安装时需要的库（processor 依赖）
- `versionInfo.libraries` — 运行时需要的库
- `processors` — 安装后处理步骤列表
- `data` — 可替换到 processor args 中的命名值

### 5. 下载 Libraries

**库来源:**
1. 顶层 `libraries`（安装时）— 下载到标准的 `libraries/` Maven 路径
2. `versionInfo.libraries`（运行时）— 同上
3. 原版 vanilla JSON 的 `libraries` — 同上
4. 原版 `client.jar` — 下载到 version 目录

**下载方式:**
- 复用现有的 `DlTask` + `dl_parallel()`（8 线程）+ `retry_failed()`（最多 MAX_RETRIES=2 次）
- 对于带有 `url` 字段的 library，直接使用其 URL
- 对于没有 `url` 的，回退到 Maven 坐标构建路径（优先 Forge Maven，其次 Mojang Maven）

### 6. 执行 Processors

这是纯手动方案的核心。每个 processor 单独执行：

**步骤:**
1. 从 processor 的 `jar` 字段解析 library → 在磁盘上找到 jar
2. 从 jar 的 `META-INF/MANIFEST.MF` 提取 `Main-Class`
   - 使用 `unzip -p <jar_path> META-INF/MANIFEST.MF` → 解析 `Main-Class:` 行
3. 构建 classpath：processor jar + `classpath` 数组中每个 library 的磁盘路径，用 `:` 连接
4. 解析 args：
   - `{MINECRAFT_JAR}` → 原版 client.jar 路径
   - `{SIDE}` → `"client"`
   - `{BINPATCH}` → Forge binpatches 文件路径
   - `{ROOT}` → version 目录
   - 从 `data` 段的 `"client"` 子键中解析
   - 对于 `[group:artifact:version]` 格式 → 解析为 Maven 路径
   - 对于 `[group:artifact:version@ext]` 格式 → 同上，保留 `@ext` 后缀
5. `fork()` + `execvp("java", args[])`
6. 父进程 `waitpid()`，检查退出码
7. 失败时重试一次（注意：processor 在 `install_profile.json` 中可能有 `outputs` 映射，重试前需清理输出文件）

**信号处理:**
- 与现有 `do_install()` 相同：`sigaction(SIGINT, ...)`，processor 执行循环中检查 `g_cancel`
- 如果已取消，`kill(pid, SIGKILL)` 中止当前 processor

**MainClass 提取函数:**
```c
static int get_jar_main_class(const char *jar_path, char *out, size_t out_size);
// 内部: popen("unzip -p %s META-INF/MANIFEST.MF", jar_path)
// 解析 "Main-Class: <classname>" 行
```

### 7. 生成自包含 Version JSON

**步骤:**
1. 以原版 vanilla JSON 为**基础**
2. 保留原版的 `assetIndex`、`downloads.client`、`assets`
3. 从 `versionInfo` 覆盖 `mainClass`（`cpw.mods.modlauncher.Launcher`）
4. 合并 `arguments`：
   - JVM: 原版 jvm args + versionInfo jvm args（追加，不覆盖）
   - Game: 原版 game args + versionInfo game args（追加）
5. 合并 `libraries`：
   - 收集 `versionInfo.libraries` + 需要运行时保留的顶层 `libraries`
   - 去重：对于 `groupId:artifactId` 相同的库，保留 Forge 提供的版本（移除原版旧版本）
   - 追加原版独有的 libraries
6. 移除 `inheritsFrom`
7. 设置 `"id"` 为 `custom_name`
8. 添加元数据 `"gameVersion": "<mc_version>"` 和 `"forge": {"version": "<forge_version>"}`（或 `"neoforge"`）
9. 写入 `<version_dir>/<custom_name>.json`

### 8. 下载 Assets

- 复用现有 `do_install()` 的 Phase 3 逻辑
- 下载 asset index → 解析 objects → 多线程下载（使用配置的 threads 值）

## 代码改动范围

### 新增文件
- 无（所有改动在 `src/version/install.c` 和 `src/version/install.h` 中）

### 修改文件

**`src/version/install.h`:**
- 新增 `ForgeConfig` 结构体
- 新增 `do_forge_install()` 函数声明

**`src/version/install.c`：**
- 在 `install_page()` 中，将 forge/neoforge 分支从 "Coming soon" 替换为 `do_forge_install()` 调用
- 新增 `parse_maven_xml()` — 解析 maven-metadata.xml
- 新增 `pick_forge_version()` — ncurses 版本选择器
- 新增 `get_jar_main_class()` — 从 MANIFEST.MF 提取 Main-Class
- 新增 `resolve_data_value()` — 解析 install_profile data 段中的值
- 新增 `run_forge_processor()` — 执行单个 processor
- 新增 `merge_forge_version_json()` — 合并 vanilla + Forge JSON
- 新增 `do_forge_install()` — Forge/NeoForge 安装主函数

### 不修改的部分
- `dl_one()`, `dl_parallel()`, `retry_failed()` — 下载系统直接复用
- `lib_rules_ok()`, `build_lib_task()` — library 处理复用
- `init_versions()` — 已能检测 `forge`/`neoforge` 字段
- `begin_version()` — 启动流程无需改动

## 错误处理

1. **Maven metadata 拉取失败** → 提示用户检查网络，返回安装页面
2. **install_profile.json 格式异常** → 输出错误信息，清理临时文件，返回安装页面
3. **Library 下载失败** → 复用现有重试机制，最终汇总失败列表
4. **Processor 执行失败** → 输出 stderr，重试一次，仍失败则中止安装
5. **SIGINT 取消** → `g_cancel` 标志中止当前 processor，清理临时文件
6. **MainClass 提取失败** → 回退到遍历 classpath 中每个 jar 查找 manifest

## Processor 依赖关系说明

Forge 的 processor 之间存在严格的输入/输出依赖，必须顺序执行：

```
Processor 1 (SideStripper)
  输入: {MINECRAFT_JAR}
  输出: {SIDE_STRIPPER_OUTPUT} (client-only jar)
        │
        ▼
Processor 2 (InstallerTools)
  输入: {SIDE_STRIPPER_OUTPUT}, {BINPATCH}
  输出: 打补丁后的 jar
        │
        ▼
Processor 3+ ...
  输入: 前一步的输出
  输出: 最终 client jar 和其他文件
```

这就是为什么不能并行执行 processor——后续 processor 依赖前面 processor 的输出文件。
