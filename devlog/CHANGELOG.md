# TMCL 开发日志

## 2026-10-31 — 版本配置三个子页顶部显示版本信息

### 改进
- ✅ 版本配置子页的 Java 选择器 / Memory 输入 / JVM Args 输入顶部统一加第 0 行 `Version Config: <版本名>`（col 2），右端提示右对齐（`tap [q] to back` / `tap [Enter] to save`，窗口宽度不足 21 列时不画）
- ✅ 子页标签下沉到第 1 行 col 3（`Java Path` / `Memory` / `JVM Args`），col 21 起显示当前值 / `global: <值>` / `empty`；Memory 提示行第 3 行、输入 `> ` 第 5 行
- ✅ 右侧值统一用 `mvaddnstr(1, 21, ..., col - 21)` 截断，避免长 JVM 参数换行覆盖下面的提示行
- ✅ Memory / JVM Args 第 1 行与 Java 选择器一致改为 `Current: <值>`（未覆盖时 Memory 显示 `Current: global: <全局值>`、JVM Args 显示 `Current: empty`）
- ✅ Memory / JVM Args 空输入直接 Enter = 取消：不修改值、不写 `tmcl.json`
- ✅ Memory / JVM Args 输入 `global`（不区分大小写）= 清除版本级覆盖、回到继承全局（与 Java 选择器的 Global 等价）
- ✅ Memory 输入校验：只接受 `auto` 或 512–32768 的纯十进制整数；非法值（`afdsah`、`100`、`32769`、`1 024`）不保存，第 7 行红色提示后重新输入

### 说明
- 100x24 与 80x12 实测三个子页标题、截断、输入与保存均正常

## 2026-10-31 — 版本级 Java 选择器 + 三态 java 覆盖

### 新增
- ✅ `<version_dir>/tmcl.json` 的 `java_path` 改为三态：空（键不存在）= 继承全局；`"auto"` = 忽略全局、强制按 `javaVersion.majorVersion` 探测；其他值 = 直接作 java 路径（`src/version/version.c` 的 `begin_version`）
- ✅ 版本配置子页 Java 行改为选择器 `pick_version_java_path`：`Global (<全局值>)` / `auto` / `Detected` / `User added`，无底部操作栏（`j`/`k`/方向键 + Enter，`q` 取消）
- ✅ `detect_java_paths()` 从 `src/config/config.c` 导出（去 `static`，`config.h` 加声明），供版本选择器复用
- ✅ 版本配置页 Java / Memory 行未覆盖时显示 `global: <全局值>`，JVM Args 空值显示 `empty`

### 说明
- 实测四种 Java 分支日志均正确：`(version override)`、`(version override: auto, major version: N)`、`(global config)`、`(required major version: N)`
- 选择器在 100x24 与 80x12 下滚动、`...more above/below...`、Enter 写入与重启读回均通过
- 过程中修掉两处自身 bug：选中 Global 时 `sel - 2` 为负导致写入垃圾路径（补 `sel >= 2` 判断）；`max_display` 比实际可绘行多 1 导致末项不可见（`row - 4` → `row - 5`）

## 2026-10-31 — java_path 子页面视觉统一 + 界面文案英文化

### 改进
- ✅ `src/config/config.c` `change_java_path` 对齐主页面视觉：第 0 行 `tap [q] to back`、第 1 行 `Java Path` / `Current: <值>`、列表从第 3 行起，去掉散文表头与两条横线
- ✅ 底部操作栏 `[c]hoose [n]ew [d]elete` 加反色光标；`h`/`l` 与左右方向键移动，Enter 执行光标所在操作（`c`/`n`/`d` 快捷键仍可用）
- ✅ 操作提示移到倒数第二行顶格：错误红色（`COLOR_PAIR(1)`），成功默认色；新增 `msg_err`，每次 `getch()` 后重置，避免上一条的错误色粘到成功提示
- ✅ 界面文案英文化：java 路径选择器 5 条提示 + `src/version/version.c` 的 3 条启动/退出日志

### 说明
- 中文只保留在注释里（项目约定注释用中文）；真正的 i18n 仍列在 `devlog/TODO.md`「待排期」

## 2026-10-17 — 警告清零（-Wall 0 warning 目标达成）

### 清理
- ✅ 84 条警告全部清零，`make clean && make` 0 warning 0 error
- ✅ install.c 36 条：10 处 `||`/`&&` 混合表达式加显式括号（保持语义）；缓冲区扩容（DlTask.dest 512→1024、ext_cmd 1600→4096、ndir 512→1024、unzip cmd 2048→4096、NeoForge 前缀缓冲 64→96/128）；lbl 前缀拼接 300→350 + 拷回精度截断；4 处删除未用 row/col + 3 处 (void)
- ✅ account.c 13 条：7 处字符字面量中的原始 NUL 字节（`'<NUL>'`，历史上误以为等价 `'\0'`）改为 `\0` 转义序列；删除未用变量 sp 及 3 处 row/col；1 处 (void)
- ✅ version.c 14 条：jsonName/jarName 256→512、json_path 350→1600、injector_path 600→1200、mv_cmd 1280→2048、dcmd 2100→4096 等 7 处扩容；1 处删除 + 3 处 (void)
- ✅ mod.c 9 条：mod_init 三处 struct 拷贝改 `%.*s` 精度截断（源字段声明 2048，真实内容 <300）；mod_page 路径缓冲 1024→1040
- ✅ mod_version_page 3 条：install_as/dest 1024→1280、cmd 2048→4096
- ✅ new_mod.c 4 条：3 处 (void) + loader 清空改直接赋值
- ✅ config.c 3 条、main.c 1 条、home.c 1 条：删除未用声明 + (void) 标注

### 备注
- 缓冲区全部按 GCC 可证明的最坏情况计算，`%.*s` 截断处真实内容远小于容量，无实际截断
- 分 8 个 `refactor:` 提交，每文件独立验证

## 2026-08-17 — java_path 三栏选择器

### 新增
- ✅ java_path 配置改为三栏选择器：auto（启动时自动选择）/ 自动检测（扫描 /usr/lib/jvm + PATH，realpath 去重）/ 用户添加，默认值 `auto`
- ✅ 操作栏 [c]hoose / [n]ew / [d]elete：new/delete 仅作用于用户添加的路径，new 校验存在 + 可执行 + realpath 不重复，delete 需 y/N 确认
- ✅ 用户路径持久化到 tmcl.json 隐藏键 `java_paths`（JSON 数组）
- ✅ 启动优先级调整：版本级覆盖 > 全局 java_path（非 auto）> javaMajor 检测 > /usr/bin/java 兜底（此前全局设置被 javaMajor 检测遮蔽，几乎无用）

### 测试
- ✅ tmux 冒烟测试：三栏渲染、光标定位当前值、choose/auto、重复路径拒绝（含 /usr/bin/java 软链接场景）、非法路径提示、delete 权限限制、JSON 持久化与还原

## 2026-08-17 — 修复 new_mod.c format-truncation 警告

### 清理
- ✅ NewModState.mod_id / mc_version、ModVersionState.mc_version 由 32 扩为 64，与上游 ModResult.id[64]、输入缓冲 input[64] 对齐，消除 2 处 format-truncation 警告；gameVer_param 64→96 消除级联警告。过滤词 UI 上限仍为 32 字符（行为不变）

## 2026-08-17 — 修复启动游戏后改变终端大小导致 UI 提前重绘

### 修复
- ✅ begin_version waitpid EINTR 重试：ncurses 安装的 SIGWINCH 处理器（sa_flags=0，无 SA_RESTART）在 endwin 后仍生效，游戏运行期间改变终端大小会中断阻塞中的 waitpid（返回 -1/EINTR），代码未检查返回值误判游戏已退出 → 返回主循环重绘 UI 覆盖日志，游戏仍在后台运行。现按 install.c 下载模式循环重试 EINTR

## 2026-06-20 — 账户系统 + 版本配置

### 已完成
- ✅ 账户系统：离线账户 CRUD + UUID v3 生成 + 持久化
- ✅ LittleSkin / 自定义 Yggdrasil 登录
- ✅ 启动时自动注入 authlib-injector（皮肤支持）
- ✅ 离线皮肤（CustomSkinLoader `-Dcustomskinloader.skinPath`）
- ✅ Yggdrasil token 自动刷新（/authserver/validate + /authserver/refresh）
- ✅ 版本级配置：[c]onfig 子页面（Java / Memory / JVM Args 覆盖）
- ✅ check & complete：扫描并补全缺失的 library + asset 文件
- ✅ rename / delete 移入版本 config 子页面
- ✅ Config 新增 isolate 设置（版本目录隔离 vs 共享 .minecraft）
- ✅ 安装器 PK + zip END header 完整性校验

### 技术债务
- ⬜ Microsoft 登录（等待可用 client ID）
- ⬜ 版本安装不支持 Forge/Fabric/NeoForge
- ⬜ [m]od 按钮无功能
- ⬜ NeoForge 启动支持
