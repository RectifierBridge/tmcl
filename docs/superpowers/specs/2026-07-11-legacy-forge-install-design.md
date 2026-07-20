 # 旧版 Forge 版本安装设计

 日期: 2026-07-11
 目标: 在 TMCL 中实现旧版 Forge（1.7.10–1.12.2，launchwrapper 体系）的版本安装与启动

 ## 概述

 旧版 Forge（1.7.10–1.12.2）使用 launchwrapper 体系，与新版的 modlauncher/processor 体系完全不同。`install_profile.json` 无 `processors`，版本 JSON 通过 `inheritsFrom` 继承原版，主类为 `net.minecraft.launchwrapper.Launch`，启动参数使用 `minecraftArguments` 字符串格式。

 采用独立函数 `do_legacy_forge_install()`，与现有 `do_forge_install()`（Forge 1.13+/NeoForge）完全分离。版本选择复用现有的 Maven XML 解析器和选择器 UI，下载系统复用 `build_lib_task()`、`dl_parallel()`、`retry_failed()`。

 ## 覆盖范围

 - Forge 1.7.10、1.8.x、1.9.x、1.10.x、1.11.x、1.12.x（所有旧版 launchwrapper 体系）
 - 不覆盖 Forge 1.13+ 和 NeoForge（已有 `do_forge_install()`）


## 路由检测

`install_page()` 在用户选择 forge + 特定 MC 版本后，根据 MC 版本的第二个数字（minor）判断走旧版还是新版：

- 提取 `selected_version` 中的 minor 版本号（如 `1.12.2` → `12`，`1.7.10` → `7`，`1.20.1` → `20`）
- minor ≤ 12 → 调用 `do_legacy_forge_install()`
- minor ≥ 13 → 调用现有 `do_forge_install()`

新增 `static int is_legacy_forge(const char *mc_version)` 辅助函数做此判断。

 ## 安装流程

 ```
 Phase 0: 版本选择（install_page 中已完成 — 复用 parse_maven_xml + pick_forge_version）
 Phase 1: 下载 installer jar → 提取 install_profile.json
 Phase 2: 确保原版 vanilla JSON 存在于 versions/<mc_ver>/<mc_ver>.json（不存在则下载）
 Phase 3: 并行下载 vanilla libraries + forge libraries + vanilla client.jar
 Phase 4: 生成 forge 版本 JSON（保留 inheritsFrom，添加 forge 元数据）
 Phase 5: 下载 assets
 ```

 ### Phase 1：下载 installer jar 并提取 install_profile.json

 - URL 构建：`https://maven.minecraftforge.net/<group_path>/forge/<forge_ver>/forge-<forge_ver>-installer.jar`
 - 下载到 `/tmp/tmcl_forge_legacy_installer_<forge_ver>.jar`
 - 用 `unzip -p` 提取 `install_profile.json` 到内存，cJSON 解析

 ### Phase 2：确保原版 JSON 存在

 - 检查 `$MCDIR/versions/<mc_version>/<mc_version>.json`
 - 若不存在：用 `vanilla_url`（从 `install_page` 传入，即 Mojang manifest 中该版本的 JSON URL）下载并写入
 - `vanilla_url` 已是 HTTPS（`piston-meta.mojang.com`），无需额外处理

 ### Phase 3：并行下载 libraries 和 client.jar

 **来源收集：**
 1. 原版 vanilla JSON 的 `libraries` 数组 → 构建 `DlTask`
 2. Forge installer 中 `versionInfo.libraries` → 构建 `DlTask`（Forge 库条目通常自带 `url` 指向 `files.minecraftforge.net`，`build_lib_task` 会优先使用）
 3. 原版 `client.jar`（从 vanilla JSON 的 `downloads.client.url` 提取）→ 下载到 `versions/<mc_version>/<mc_version>.jar`
 4. Asset index JSON（从 vanilla JSON 的 `assetIndex.url`）

 **下载方式：** 复用 `dl_parallel()`（8 线程）+ `retry_failed()`（最多 2 次）。`build_lib_task` 已支持 `group:artifact:version:classifier` 格式，覆盖旧版 Forge 的 `:universal` classifier。

 ### Phase 4：生成 forge 版本 JSON

 以 installer 的 `versionInfo` 为基础，做以下修改后写入 `versions/<custom_name>/<custom_name>.json`：

 | 字段 | 处理方式 |
 |---|---|
 | `id` | 设为 custom_name |
 | `inheritsFrom` | 保留为原版 MC 版本 ID（如 `"1.12.2"`） |
 | `minecraftArguments` | 保留原样（旧版字符串格式） |
 | `mainClass` | 保留（`net.minecraft.launchwrapper.Launch`） |
 | `libraries` | 保留（Forge 专用库列表） |
 | `forge`（新增） | `{"version": "<forge_ver>"}` — 供 `version.c` 做 modloader 检测 |

 ### Phase 5：下载 assets

 复用现有 asset 下载逻辑：读取 asset index → 解析 objects → 并行下载缺失文件。

 ## 不改动 version.c

 启动系统已完整支持旧版：

 - `inheritsFrom` 链：`append_json_libraries()` 读取父版本 JSON，合并 libraries 和 jar
 - `mainClass` 提取：已从 child JSON 读取，回退父版本
 - `minecraftArguments`：`legacy_args()` 处理空格拆分的旧版参数字符串
 - `forge` 字段检测：`init_versions()` 已识别 `forge` modloader

 ## 代码改动范围

 ### 修改文件

 **`src/version/install.h`：**
 - 新增 `do_legacy_forge_install()` 函数声明

 **`src/version/install.c`：**
 - `install_page()` 中的 forge 安装分支增加 MC 版本检测，旧版路由到 `do_legacy_forge_install()`
 - 新增 `do_legacy_forge_install()` 函数（约 200-250 行）

 ### 依赖的已有函数（无需修改）

 - `parse_maven_xml()` — Maven metadata 解析
 - `pick_forge_version()` — 版本选择器 UI
 - `build_lib_task()` — library 路径和 URL 构建（已支持 classifier）
 - `lib_rules_ok()` — library rules 过滤
 - `dl_parallel()` / `retry_failed()` / `dl_one()` — 下载系统
 - `legacy_args()` — minecraftArguments 处理

 ## 错误处理

 1. **Installer jar 下载失败** → 输出错误，goto done 清理
 2. **install_profile.json 解析失败** → 输出错误，清理临时文件
 3. **原版 JSON 下载失败** → 致命错误，无法继续（inheritsFrom 依赖）
 4. **Library 下载失败** → 重试 2 次，汇总失败列表
 5. **Asset 下载失败** → 重试 2 次，汇总失败列表
 6. **SIGINT 取消** → `g_cancel` 标志中止当前操作，清理临时文件

 ## 与原版关系

 用户不需要手动安装原版。`do_legacy_forge_install()` 自动检查并下载原版 JSON 和 libraries。原版 JSON 以官方 Mojang 版本 JSON 形式写入 `versions/<mc_ver>/<mc_ver>.json`（与 Mojang 官方 Launcher 格式一致），不附加任何 TMCL 特有字段。
