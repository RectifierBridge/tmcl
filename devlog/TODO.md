# TODO — 开发待办

## 警告清零（-Wall 0 warning 目标）

现状：2026-08-17 全量重建统计 84 条警告。分布：
- install.c ~35 条（format-truncation 缓冲不足、`||` 中 `&&` 无括号、unused row/col）
- version.c ~14 条、account.c ~13 条（truncation、空字符字面量、unused）
- mod.c 9 条（路径拼接 truncation）、config.c / mod_version_page.c / new_mod.c 各 3-4 条
- main.c / home.c 各 1 条（unused）

- [ ] install.c 集中清理（truncation 缓冲区审计 + parentheses + unused）
- [ ] 其余模块逐文件清零
- [ ] `make clean && make` 0 warning 验证

## V5 — java_path 选择器

现状：启动时 javaMajor 检测优先于全局 java_path，导致全局设置几乎无用。改为选择器 UI，java_path 默认值 `auto`。

- [x] 存储：ConfigState 新增 `user_java_paths[32][512]` + count，tmcl.json 新增隐藏键 `java_paths`（JSON 数组，config_write / config_init 读写），java_path 默认值改为 `auto`
- [x] 检测：`detect_java_paths()` 扫描 /usr/lib/jvm/*/bin/java（含 jre 变体）+ PATH 上的 java，realpath 去重
- [x] UI：change_config 中 java_path 分支改为三栏选择器（auto / detected / user added），操作栏 [c]hoose [n]ew [d]elete（new/delete 仅限用户栏），[n]ew 校验存在+可执行+realpath 不重复
- [x] 启动优先级：版本级覆盖 > 全局 java_path（非 auto）> javaMajor 检测 > /usr/bin/java 兜底

## V1 — Version Config 子页面 ✅

详见 `docs/specs/version-config.md`

- [x] VersionInfo 新增 java_override / memory_override / jvm_args_extra
- [x] 读取/写入 `<version_dir>/tmcl.json`
- [x] Config 子页面 UI（java / memory / jvm / check / rename / delete）
- [x] 主页面底部栏改为 [b] [m] [p] [c] [n]（5 项）
- [x] begin_version 读取版本级覆盖配置
- [x] check & complete：扫描并补全缺失的 library + asset

---

## V2 — NeoForge 启动支持

NeoForge 是 Forge 的后继者（社区分裂产物），启动机制与 Forge 完全相同：
- `inheritsFrom` 继承原版
- `mainClass` 同为 `cpw.mods.modlauncher.Launcher`
- 使用现代 `arguments` 格式

改动很小，主要是 modloader 检测（JSON 中有 `neoforge` 字段）。

- [x] init_versions 识别 neoforge 字段 → modloader = "neoforge"
- [x] 版本列表显示 neoforge 类型

## V3 — 模组版本安装

在安装页面支持选择模组加载器（vanilla / fabric / forge / neoforge），根据选择下载对应加载器并合并到版本 JSON。

- [x] 安装页面新增 modloader 选择行（默认 vanilla）
- [x] Fabric 安装：从 Fabric meta API 拉取 loader 版本
- [x] Forge 安装：从 Forge maven 拉取版本列表
- [x] NeoForge 安装：从 NeoForge maven 拉取
- [x] 下载 loader JAR + 合并 libraries

## V4 — Mod 页面

版本页面 `[m]od` 按钮的功能：浏览版本目录下的 `mods/` 文件夹，启用/禁用模组。

- [x] 扫描 `<version_dir>/mods/` 下的 .jar 文件
- [x] 列表显示，支持启用/禁用（重命名 .jar → .jar.disabled）
- [ ] 支持打开 mods 文件夹（未实现：mod.c 无打开文件管理器代码）
