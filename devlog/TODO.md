# TODO — 开发待办

## 警告清零（-Wall 0 warning 目标）

现状：2026-10-17 全量重建统计 84 条警告。分布：
- install.c 36 条（truncation、`||` 中 `&&` 无括号、unused row/col）
- version.c 14 条、account.c 13 条（truncation、NUL 字节字面量、unused）
- mod.c 9 条、new_mod.c 4 条、config.c / mod_version_page.c 各 3 条
- main.c / home.c 各 1 条（unused）

### 全局约束（本计划所有任务通用）
- 行为保持不变：仅消除警告，不改功能语义；括号类按 GCC 建议加括号，保持既有 `||` 优先语义
- 缓冲区放大遵循 CLAUDE.md 缓冲区表；无法放大的用 `%.*s` 精度截断（真实内容远小于缓冲区，无实际截断）
- 未使用变量项目惯例 `(void)var;`（mod_version_page.c:20 已有先例）；整行未用则删除声明+getmaxyx
- 每个任务完成 → `make clean && make` 验证对应文件 0 warning → 一个 `refactor:` 提交

### Task 1: main.c + home.c（2 条）
- [x] main.c:143 title_bar getmaxyx 后加 `(void)row;`（col 在 152 使用）
- [x] home.c:114 home_page getyx 后加 `(void)cur_col;`（cur_row 在 115 使用）

### Task 2: config.c（3 条）
- [x] config.c:87 删除未使用的 `cJSON *item;`（循环内 89 行已重新声明）
- [x] config.c:568-569 change_config 删除 `int row, col;` 与 getmaxyx（均未使用）

### Task 3: account.c（13 条）
- [x] 7 处字符字面量内原始 NUL 字节（629/631/633/636/700/877/971 的 `'<NUL>'`）改为 `'\0'` 转义序列：`perl -pi -e "s/'\x00'/'\\0'/g" src/account/account.c`（已全量扫描，仅此文件含 NUL 字节）
- [x] 363 删除 `cJSON *sp = cJSON_GetObjectItem(j, "selectedProfile");`（未使用，选择逻辑走 availableProfiles）
- [x] 390-391 ygg_login 角色选择器循环内删除 `int row, col;` 与 getmaxyx（均未使用）
- [x] 441 ms_full_login getmaxyx 后加 `(void)col;`（row 在 621/647 使用）
- [x] 1189-1190 delete_account_popup 删除 `int row, col;` 与 getmaxyx（均未使用）

### Task 4: install.c（36 条）
- [x] 10 处括号（94/96/369/371/494/496/939/941/1184/1186）：`c == 'j' || c == KEY_DOWN && sel < N` → `c == 'j' || (c == KEY_DOWN && sel < N)`；'k'/KEY_UP 同理
- [x] pick_modloader 80：`int sel = cur, row, col;` → `int sel = cur;`；83 删除 getmaxyx
- [x] pick_type 355 删除 `int row, col;`；358 删除 getmaxyx
- [x] input_name 516-517 删除 `int row, col;` 与 getmaxyx
- [x] install_page 132 getmaxyx 后加 `(void)row;`（col 在 136 使用）
- [x] pick_version 470、pick_fabric_loader 915、pick_forge_version 1160 getmaxyx 后各加 `(void)col;`（row 均在下方使用）
- [x] parse_maven_xml 983：`char base_prefix[64]` → `[96]`、`char prefix[64]` → `[128]`（base ≤ 63、base_prefix ≤ 95、prefix 最坏 104）
- [x] resolve_data_value 1298：`char ext_cmd[1600]` → `[4096]`（tmp_path×2 + inner×2 + 字面量 ≈ 3284）
- [x] DlTask.dest（550 行）：`[512]` → `[1024]`（version_dir 511 + "/" + ".jar" + NUL = 517；client_jar_path 599；本地 dest 599）—— 修 2046/2516/2739/3110 四条
- [x] 5 处 `char lbl[300]`（2075/2493/3094/3134/3156）→ `[350]`；其后 `snprintf(t->label, sizeof(t->label), "%s", lbl);` 改为 `"%.*s"` 精度 `(int)sizeof(t->label) - 1`（防 lbl 扩容引入新警告）
- [x] do_legacy_forge_install 2576：`char ndir[512]` → `[1024]`（version_dir 511 + "/natives" 8 + NUL = 520）

### Task 5: version.c（14 条）
- [x] 83-84：`char jsonName[256]` / `char jarName[256]` → `[512]`（d_name 255 + 后缀 5）
- [x] 102：`char json_path[350]` → `[1600]`（versionPath 1023 + jsonName 511）
- [x] 691：`char injector_path[600]` → `[1200]`（home_dir 999 + 后缀 25）
- [x] 991-992 rename_version 删除 `int row, col;` 与 getmaxyx（均未使用）
- [x] 1017：`char mv_cmd[1280]` → `[2048]`（new_dir 599×2 + 名字 63/31 + 字面量 ≈ 1356）
- [x] 1031：`char json_path[600]` → `[1024]`（new_dir 599 + 名字 63 + 6）
- [x] 1223：`char dcmd[2100]` → `[4096]`（url 1023 + lib_path 511×2 + 字面量 ≈ 2130）
- [x] 1280：`char dcmd[1280]` → `[2048]`（obj_path 599×2 + first2 2 + hash 40 + 字面量 ≈ 1326）
- [x] 1329 version_config_page getmaxyx 后加 `(void)row;`（col 在 1331 使用）
- [x] 1452 version_page getmaxyx 后加 `(void)col;`（row 在 1460 使用）
- [x] 1748 bottom_bar getmaxyx 后加 `(void)col;`（row 在 1753 使用）

### Task 6: mod.c（9 条）
- [x] mod_init 24：`"%s"` → `"%.*s"` 精度 `(int)sizeof(state->version_dir) - 1`（selected_version_dir 2047 > 1024）
- [x] mod_init 33/36：`"%s/mods"` → `"%.*s/mods"` 精度 `(int)sizeof(state->mods_dir) - 6`（path 绑定时 1024 装不下 "/mods"）
- [x] mod_page 212：`char oldpath[1024], newpath[1024]` → `[1040]`（mods_dir 1023 + "/" + ".disabled" 9 + NUL = 1034）
- [x] mod_page 241：`char filepath[1024]` → `[1040]`

### Task 7: mod_version_page.c + .h（3 条）
- [x] .h：`char install_as[1024]` → `[1280]`（mods_dir 1023 + "/" + filename 255）
- [x] .c 197-198：`char dest[1024]` → `[1280]`；`char cmd[2048]` → `[4096]`（dest 1279 + download_url 1023 + 字面量 ≈ 2330）

### Task 8: new_mod.c（4 条）
- [x] 182 new_mod_page getmaxyx 后加 `(void)col;`（row 在 194 使用）
- [x] 263 mod_search_dld_json getmaxyx 后加 `(void)col;`（row 在 264 使用）
- [x] 584 action_modloader getmaxyx 后加 `(void)col;`（row 在 597 使用）
- [x] 628：`snprintf(state->loader, sizeof(state->loader), "");` → `state->loader[0] = '\0';`（-Wformat-zero-length）

### Task 9: 收尾
- [x] `make clean && make` 0 warning 验证
- [x] CHANGELOG.md 记录，本清单打勾

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

---

## V5 — 版本级 Java 选择器（2026-10-31 完成）

版本配置子页的 Java 从文本输入改为选择器，并给 `java_path` 三态语义。

- [x] `detect_java_paths()` 从 config.c 导出（去 static + config.h 声明）
- [x] `begin_version` 三态分支：空=全局 / `auto`=忽略全局按版本探测 / 路径=直接使用
- [x] `pick_version_java_path()` 选择器：Global (全局值) / auto / Detected / User added
- [x] 版本配置页 Java / Memory 行显示 `global: <值>`
- [ ] 选择器改为与 Config 页 `change_java_path` 共享实现（目前是 version.c 内的精简副本）

---

## 待排期（无优先级，按需取用）

- [ ] 中文本地化：目前 UI 全英文，需引入字符串表与语言配置
- [ ] 下载进度条：安装与模组下载期间只有 `printf` 文本日志，没有单行刷新进度
- [ ] 文件 SHA1 校验：manifest 里已有 sha1，目前只校验 PK 魔数与文件存在性
- [ ] 资源包 / 光影包管理（对应 `resourcepacks/`、`shaderpacks/`）
- [ ] BMCLAPI 下载源接线：配置项 `download_cource` 已有 UI，但 install.c 中仍是硬编码官方源的 TODO
- [ ] quilt 加载器：安装器与 `init_versions` 不认 quilt，目前只作为模组筛选条件存在
- [ ] 版本级 `memory` 输入校验：现在走 `atoi()`，填 `abc` 会得到 `-Xmx0m`（全局有 `strtol` 校验）
