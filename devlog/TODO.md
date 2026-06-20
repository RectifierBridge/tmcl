# TODO — 开发待办

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

- [ ] init_versions 识别 neoforge 字段 → modloader = "neoforge"
- [ ] 版本列表显示 neoforge 类型

## V3 — 模组版本安装

在安装页面支持选择模组加载器（vanilla / fabric / forge / neoforge），根据选择下载对应加载器并合并到版本 JSON。

- [ ] 安装页面新增 modloader 选择行（默认 vanilla）
- [ ] Fabric 安装：从 Fabric meta API 拉取 loader 版本
- [ ] Forge 安装：从 Forge maven 拉取版本列表
- [ ] NeoForge 安装：从 NeoForge maven 拉取
- [ ] 下载 loader JAR + 合并 libraries

## V4 — Mod 页面

版本页面 `[m]od` 按钮的功能：浏览版本目录下的 `mods/` 文件夹，启用/禁用模组。

- [ ] 扫描 `<version_dir>/mods/` 下的 .jar 文件
- [ ] 列表显示，支持启用/禁用（重命名 .jar → .jar.disabled）
- [ ] 支持打开 mods 文件夹
