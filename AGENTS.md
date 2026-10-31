# TMCL — Terminal Minecraft Launcher

C + ncursesw 写的 TUI Minecraft Java 版启动器，GPLv3。
主循环是**单线程 `getch()` 阻塞式**的，只有安装下载用 pthread。

## 常用命令

| 命令 | 用途 |
|------|------|
| `make` | 编译，产出 `tmcl` |
| `make clean && make` | 结构变更（改头文件、加删文件）后完整重编 |
| `./tmcl` | 运行，需支持 ncurses 的终端 |
| `make install` / `make uninstall` | 装到 / 卸自 `/usr/local/bin` |

- 编译选项 `-Wall`，**要求 0 warning**。
- **没有测试套件**。验证方式：`make clean && make` 干净编译 + 跑 `./tmcl` 手动走一遍改动涉及的页面路径（含改配置后重启读回）。

## 依赖

构建：`gcc`、ncursesw、cjson、pthread。Makefile 只链接 `-lncursesw -lcjson -lpthread`，**不链接 libcurl**。

运行时外部命令（由 `popen`/`system`/`fork+exec` 调用）：`curl`（全部 HTTP 请求）、`unzip`（解 Forge 安装包）、`java`（跑 Forge/NeoForge processors）。

```bash
sudo apt install gcc libncurses-dev libcjson-dev curl unzip
```

→ 加网络功能时**不要**引入 libcurl，沿用现有两种写法：`account.c` 的 `curl_fetch()`（popen 读响应体），或 `new_mod.c` 的 `curl -o /tmp/...json` 落盘后再 `cJSON_Parse`。

## 目录结构

```
src/
├── main.c              # 入口：ncurses 初始化、4 页导航与大循环
├── home/home.c,h       # Home 页：固定版本 / 最近游玩快速启动
├── version/
│   ├── version.c,h     # 版本扫描、列表、启动、版本配置子页、check&complete、rename/delete
│   └── install.c,h     # 安装页：manifest 拉取、多线程下载、Forge/NeoForge 安装
├── config/config.c,h   # Config 页：全局配置 + Java 路径选择器
├── account/account.c,h # Account 页：Microsoft / 离线 / LittleSkin / 自定义 Yggdrasil
├── mod/
│   ├── mod.c,h         # 模组列表：启用/禁用（重命名 jar）、删除
│   ├── mod_api.c,h     # 纯解析层：Modrinth / CurseForge JSON → 结构体（无 IO）
│   ├── new_mod.c,h     # 搜索页 + 模组详情 + 下载
│   └── mod_version_page.c,h # 模组版本筛选与下载
└── job/job.c,h         # 空占位，**未编入 Makefile**，不要参考也不要用
build/                  # .o 产物，目录结构与 src/ 对应
```

每个模块一对 `.h`/`.c`；页面状态由各自的 `*State` 结构体持有（`ConfigState`、`VersionState`、`AccountState`、`ModState`…），由 `main.c` 创建并逐层传参。


## 数据文件

| 路径 | 内容 |
|------|------|
| `~/.tmcl/tmcl.json` | 全局配置（首次运行自动 `mkdir ~/.tmcl`） |
| `~/.tmcl/accounts.json` | 账户列表，**含 access_token 明文** |
| `~/.tmcl/authlib-injector.jar` | 首次用 LittleSkin/自定义 Yggdrasil 时自动下载缓存 |
| `<game_dir>/versions/<name>/<name>.json` + `.jar` | 版本本体（两者齐备才算已安装） |
| `<game_dir>/versions/<name>/tmcl.json` | 版本级配置覆盖 |
| `<game_dir>/versions/<name>/natives*/` | 解压出的原生库 |
| `<game_dir>/mods/` 或 `<game_dir>/versions/<name>/mods/` | 模组目录，取决于 `isolate` |
| `/tmp/tmcl_*_result.json` | 模组 API 响应缓存；`/tmp/mod_idx` 是调试残留 |

## 页面与按键

**全局**：`T`/`V`/`C`/`A` 切到 Home/Version/Config/Account，`H`/`L` 前后翻页（循环），`q` 退出。这几个键在主循环里被吃掉，页面函数收不到。

| 页面 | 按键 | 底部操作串 |
|------|------|-----------|
| Home | `j`/`k` 选 Pinned/Last Play，`b` 或 Enter 启动 | `[Enter]`/`[b]` |
| Version | `j`/`k` 移动，`b` 启动，`m` 模组，`p` 固定，`c` 版本配置，`n` 安装；`h`/`l` 移光标，Enter 执行 | `[b]egin [m]od [p]in [c]onfig [n]ew` |
| Version 配置子页 | `j`/`k` 移动，Enter 打开 Java 选择器、编辑 Memory/JVM Args 或执行操作，`q` 返回 | `[ check & complete ] [ rename ] [ delete ]` |
| Version 配置·Java 选择器 | `j`/`k` 或方向键移动，Enter 选中，`q` 取消；无底部操作栏 | 无 |
| Config·java_path 子页 | `j`/`k` 移动，`h`/`l` 移底部操作光标，`c`/`n`/`d` 或 Enter 执行，`q` 返回 | `[c]hoose [n]ew [d]elete` |
| 安装页 | `j`/`k` 选行，Enter 编辑或执行，`q` 取消 | `[ start installation ]` |
| Mod | `a` 启用/禁用，`n` 进搜索页，`d` 删除，`j`/`k` 移动 | `[a]ble [n]ew [d]elete` |
| Mod 搜索（new_mod） | `i` 安装，`v` 版本筛选，`m` 加载器筛选，`s` 换源，`/` 搜索，Enter 选中 | `[i]nstall [v]ersion [m]odlader [s]ource [/]search` |
| Mod 版本（mod_version_page） | `i` 安装，`v` 版本筛选，`m` 加载器筛选，`a` 安装名 | `[i]nstall [v]ersion [m]odlader [a]s` |
| Account | `c` 选为当前，`n` 新建，`d` 删除，`s` 离线皮肤，`j`/`k` 移动 | `[c]hoose [n]ew [d]elete [s]kin` |

### 外观与操作规范

- 中括号里的是要按的键，通常是英文首字母且**区分大小写**；操作之间隔一个空格。
- 每个操作既能直接按中括号里的键，也能用 `hjkl`/方向键把光标（反色高亮处）移过去再按 Enter。
- 模组加载器有固定配色：forge 红(1)、neoforge 黄(2)、fabric 绿(3)、quilt 紫(4)，选中态用 6–9 号色对。新增文案请沿用这套中括号 + 空格分隔的写法。
- 绘制一律在 `stdscr` 上（没有 `newwin`），用 `mvprintw`/`attron(A_REVERSE)` 做高亮。

## 配置系统

### 全局配置（`~/.tmcl/tmcl.json`，items 共 11 项）

| 索引 | key | 默认值 | 说明 | 配置页显示 |
|------|-----|--------|------|-----------|
| 0 | `java_path` | `auto` | 三栏选择器：auto / 自动检测 / 用户添加 | 是 |
| 1 | `memory` | `auto`（显示 `auto(N)`） | 读 /proc/meminfo 取 70% MemAvailable，夹 512–32768 MB，失败回退 2048 | 是 |
| 2 | `game_dir` | `$HOME/.minecraft` | 游戏目录 | 是 |
| 3 | `jvm_args` | `""` | 全局追加 JVM 参数 | 是 |
| 4 | `download_cource` | `Official` | Official / BMCLAPI（**BMCLAPI 未实现**，见「已知缺口」） | 是 |
| 5 | `mod_source` | `CurseForge` | CurseForge / Modrinth | 是 |
| 6 | `lib_threads` | `16` | libraries 下载线程数 | 是 |
| 7 | `ast_threads` | `96` | assets 下载线程数 | 是 |
| 8 | `isolate` | `yes` | yes=每版本独立 saves/mods | 是 |
| 9 | `pinned_version` | `""` | 首页固定版本（系统写入） | 否 |
| 10 | `last_play` | `""` | 最后游玩版本（系统写入） | 否 |

配置页按键：`j`/`k` 移动、`c` 修改、`r` 重置、`h`/`l` 切底部两个操作、Enter 执行。改动**立即** `config_write`，没有显式保存步骤。`java_paths` 是 JSON 里独立的数组（用户手动添加的路径，`MAX_JAVA_PATHS` 32），不占 items 索引。

`java_path` 子页（`change_java_path`，在 `config_page` 内部 `while(1)` 阻塞，不返回主循环）：第 0 行是 `tap [q] to back`，第 1 行是 `Java Path` / `Current: <值>`，列表从第 3 行开始；倒数第二行是操作提示（错误红色、顶格），最后一行是反色光标操作栏。`n`/`d` 的输入提示与确认行也画在倒数第二行。

### 版本级配置（`<game_dir>/versions/<name>/tmcl.json`）

只认三个键：`java_path`、`memory`（支持 `"auto"`）、`jvm_args`。非空才写入，空值即删除覆盖。

`java_path` 是三态：空（键不存在）= 继承全局；`"auto"` = **忽略全局 `java_path`**，强制按 `javaVersion.majorVersion` 探测；其他值 = 直接作为 java 可执行文件路径。

优先级：版本级路径 > 全局 `java_path`（非 auto 时）> 按 `javaVersion.majorVersion` 探测 > `/usr/bin/java`。

版本配置子页三行的显示规则：覆盖为空时显示 `global: <全局值>`，`jvm_args` 为空时显示 `empty`；只有 Java 行按 Enter 打开选择器（`pick_version_java_path`，`version.c` 内，Global / auto / Detected / User added），Memory 与 JVM Args 仍是 `getnstr` 文本输入。

三个子页顶部统一：第 0 行 `Version Config: <版本名>`（col 2），Java 选择器在右端另有 `tap [q] to back`（Memory / JVM Args 当前不画右端提示），第 1 行 col 3 是子页标签（`Java Path` / `Memory` / `JVM Args`），col 21 起是 `Current: ...`（Java：路径或 `global: <全局值>`；Memory：覆盖值或 `global: <全局值>`；JVM Args：参数或 `empty`）。Memory / JVM Args 的提示行在第 3 行、输入 `> ` 在第 5 行。

Memory / JVM Args 的输入：直接按 Enter（空输入）= **取消**，不写盘也不改值；输入 `global`（`strcasecmp`，不区分大小写，精确匹配）= **清除版本级覆盖**，回到继承全局（与 Java 选择器的 Global 等价；`-Dglobal=1` 这类参数不会误判）；其他非空输入 = 写入覆盖。清除后键不写入 `tmcl.json`（`save_version_config` 只写非空值）。

Memory 输入还会校验：`auto`（大小写不敏感，统一存成小写 `"auto"`）或 512–32768 的纯十进制整数（`strtol` + 越界检查，存成规范化数字）；其他值（如 `afdsah`、`100`、`32769`、`1 024`）不保存，在第 7 行以红色 `COLOR_PAIR(1)` 显示 `invalid value: enter MB between 512 and 32768` 并重新等待输入。

### 新增配置项时必须同步

`config_init` 的 `keys[]`/`default_values[]` 与 `item_count`、配置页 `j`/`k` 边界（用的是 `item_count - 3`，最后两项不可选）、`change_config` 里的分支、本节表格。

## 关键机制

### 启动（`begin_version`）

写 `last_play` → `endwin()` → `classpath_from_json()`（16384 字节静态 buffer，支持 `inheritsFrom` 继承链）→ 读子/父 JSON 决定 `mainClass`、`assetIndex` → 定 `gdir`（`isolate=no` 用 game_dir，否则版本目录）→ 找 `natives*` 子目录（回退 `versions/<name>/natives`）→ 刷账户 token → 离线皮肤 `-Dcustomskinloader.skinPath` → authlib-injector（仅 littleskin/third_party，缺 jar 时从 authlib-injector.yushi.moe 下载，`-cp` 之前注入）→ `-Xmx` → Java → JSON 里的 `arguments.jvm`（`sub_arg()` 替换 `${...}` 占位符）→ 版本级 `jvm_args` → `-cp <classpath>` → mainClass → 游戏参数（`arguments.game`，旧格式走 `minecraftArguments`）。

`args[300]` 与 `tofree[300]` 是硬上限：新增参数必须同时登记进 `tofree`，否则泄漏；接近上限处要判 `ac < 300 - n`。

### 安装（`install.c`）

安装页选 type/version/modloader/name（modloader 只有 vanilla/fabric/forge/neoforge）→ `do_install`：线程数取 `lib_threads`/`ast_threads`（<1 时**都回退 96**）→ 下版本 JSON → Fabric/Forge/NeoForge 合并成**自包含 JSON**（去掉 `inheritsFrom`，按 groupId:artifactId 去重，加载器版优先）→ 任务队列（client.jar、libraries、asset index）→ `dl_parallel` → 失败重试最多 `MAX_RETRIES=2` 轮 → assets 按哈希前两字符分目录从 `resources.download.minecraft.net` 拉（已存在则跳过）→ JAR 校验（PK 头 + ZIP END 头，坏则删）。

Forge/NeoForge 现代版：`unzip` 取 `install_profile.json` → 跑 processors（fork/exec java，processor classpath 32768 字节）→ 合并 versionInfo。旧版 Forge（1.7.10–1.12.2）：`do_legacy_forge_install`，launchwrapper 体系，无 processors。

SIGINT 取消靠全局 `g_cancel`，每 100 ms 轮询，kill 当前 curl/子进程并跳过剩余任务。

改安装逻辑前要知道的细节：

- Maven URL 是把 groupId 的 `.` 换成 `/` 拼出来的；`build_lib_task()` 里把 `http://` 强制升级成 `https://`（部分仓库已禁 HTTP）。
- NeoForge 版本号映射：MC `1.21`（两段）→ NeoForge 前缀 `21.0.`，MC `1.21.1` → `21.1.`；带 `-pre-N`/`-snapshot-N`/`-rc` 的快照走 `X.Y.0.0-alpha.` 前缀并精确匹配 `+pre-N` 后缀；MC 正式版会过滤掉含 `alpha`/`snapshot` 的 NeoForge 版本（保留 `-beta`）。
- `install_profile.json` 的 `data` 段由 `resolve_data_value()` 递归解析（`{KEY}` 占位符、`[maven:coord]`、坐标、processor outputs 累积映射），它是 Forge 安装的核心函数。
- Fabric 合并时跳过 `-DFabric.*` JVM 参数以免重复；自包含 JSON 里保留 `gameVersion` 与 `forge`/`neoforge`/`fabric` 子对象，`init_versions` 靠它们判定加载器类型与版本号。改合并逻辑会直接影响版本列表显示。
- 旧版 Forge 的原生库在 `downloads.classifiers` 的 `natives-*` 条目里，不是 Maven 坐标。

### 模组

- `mod.c`：扫 `<mods_dir>/*.jar` 与 `*.jar.disabled`；启用/禁用 = rename 加/去 `.disabled`；删除 = `remove()`。`mods_dir` 由 `isolate` 决定。
- `new_mod.c`：`popen` 调 curl 把响应写到 `/tmp`，再交 `mod_api.c` 的 `mod_api_parse_search/detail/versions` 解析（该文件是纯解析层，不联网）。CurseForge 要 API key（内嵌在 `mod_api.h`）。
- 下载完成后写进 mods 目录，返回 mod 页会重新扫描。

### 账户

四种 `type`：`microsoft` / `offline` / `littleskin` / `third_party`（自定义 Yggdrasil）。
Microsoft：device code（`nodelay` 轮询，这是全项目唯一的非阻塞输入）→ XBL → XSTS → Minecraft login → profile。
离线：`OfflinePlayer:<name>` → md5 → UUID v3。Yggdrasil：`/authserver/authenticate` + 多角色选择。
启动前统一 `account_refresh_token()`（MS 走 refresh token 链，Yggdrasil 走 validate+refresh）。

### ncurses 生命周期

`begin_version`、`do_install`、`check_complete` 会 `endwin()` 后用 `printf` 打日志。返回主循环后 `isendwin()` 为真 → 重新 `initscr()` 并刷新 `init_versions` 与 `home_init`。子函数里不要再调 `initscr()`；需要光标/回显时用 `echo()`/`curs_set(1)` 并成对还原。

## 代码约定

- 缩进 2 空格，不用 tab；注释用中文。
- 命名：函数 `snake_case`，结构体 `PascalCase`，宏 `UPPER_SNAKE`。
- 缓冲区：一律 `snprintf` + `sizeof`，写入前判边界。这里常见的坑是「声明尺寸 ≫ 实际内容」（`selected_version_dir` 声明 2048，内容 <300；`mods_dir` 用 `%.*s` 按容量截断）。常用尺寸：局部路径 256、classpath 16384、processor classpath 32768、args 300、URL/命令 1024–2100。
- 内存：每个 `malloc` 都要有 `free`，各模块提供 `*_cleanup`；临时参数用 `tofree` 池集中释放。
- 头文件：`#ifndef`/`#define`/`#endif` 保护，宏名对应文件名（如 `CONFIG_H`）。
- 没有格式化或 lint 配置，跟随所在文件既有风格。

### 常见模式

页面函数：先处理按键、再绘制；签名固定为 `(int ch, int *middlep, XxxState *state, ...)`，由 `main.c` 调用。

```c
void xxx_page(int ch, int *middlep, XxxState *state) {
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);
  switch (ch) { /* 先处理按键 */ }
  mvprintw(2, 4, "..."); /* 再画 UI */
}
```

弹出选择器（`pick_*` 系列全是这个形状）：`while (1)` + 内部 `getch()`，Enter 确认、`q` 恢复原值返回。

## 已知缺口（动手前先看）

- **`download_cource` 的 BMCLAPI 没接线**：只有配置项 UI 和 install.c 里一句 TODO 注释，所有下载 URL 硬编码官方源。
- **quilt 不可达**：`modloader_color` 有 quilt 配色，但安装器与 `init_versions` 都不认 quilt，它只作为模组筛选器存在。
- **`[p]in` 只能设置不能取消**：`pin_version` 直接写 `items[9]`，没有切换逻辑。
- `config.c` 里 `game_dir` 输入后调的是 `remove_trailing_slash("input")`（字面量），应传变量 `input`。
- `config.c` 把 `h`/`l`/Enter 的 `case` 标签嵌在 `r` 分支的 `if` 体内（能跑，改这段时别被绕进去）。
- `version.c` 的 `mod()` 在索引非法时会 `system()` 写 `/tmp/mod_idx`（调试残留）。
- `src/job/` 未编入 Makefile；`main.c` 只 include 了它的头文件。
- **无 i18n**：所有界面文案是英文硬编码在 C 字符串里，注释才是中文；本地化（中文本地化）在 `devlog/TODO.md` 待排期。
- 版本级 `memory` 的 UI 已校验（512–32768 / `auto` / `global`），但 `begin_version` 仍用 `atoi()` 读 JSON：手改成 `abc` 会得到 `-Xmx0m`。全局 `memory` 只有 `strtol` 纯数字校验，没有上下限。

## 开发流程

1. **讨论** — 用中文谈需求、方案与优先级，有分歧先对齐再动手。
2. **写计划** — 计划落到 `devlog/TODO.md`，写成可勾选清单，注明要改的文件与验证方式。
3. **实现** — 小步修改，每步 `make` 验证。
4. **验证** — `make clean && make` 0 warning，再跑 `./tmcl` 走一遍受影响路径。
5. **记录** — 完成的工作写进 `devlog/CHANGELOG.md`。

## Git 与提交

- **不要自行 commit/push**，等用户明确指示；用户审查后才合并。
- 前缀：`feat:` / `fix:` / `refactor:` / `docs:`；摘要行 ≤72 字符，正文说明改了什么、为什么。
- 功能在特性分支开发（如 `forge/neoforge`），稳定后合入 `main`。
- 许可证 GPLv3，所有贡献必须保持 GPL 兼容。

## 其他

使用中文与用户会话。
