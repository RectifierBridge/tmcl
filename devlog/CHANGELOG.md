# TMCL 开发日志

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
