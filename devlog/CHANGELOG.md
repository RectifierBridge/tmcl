# TMCL 开发日志

## 2026-06 — 启动器核心功能

### 已完成
- ✅ 原版 Minecraft 启动（classpath 构建、JVM 参数处理）
- ✅ Forge 1.7.10+ 启动（launchwrapper 继承链、Java 8 自动匹配）
- ✅ Fabric 1.21+ 启动（KnotClient、inheritsFrom classpath 合并）
- ✅ 版本页面 [b]egin / [m]od / [p]in / [r]ename / [n]ew / [d]elete
- ✅ 首页 Pinned Version + Last Play 快速启动
- ✅ 配置页面：java_path / memory / game_dir / jvm_args / download_source / mod_source / threads
- ✅ memory auto 模式（70% 可用内存）
- ✅ 版本安装：type 选择 → version 选择 → name 输入 → 多线程下载
- ✅ 下载重试（最多 2 次）+ 失败文件总结
- ✅ Ctrl+C 优雅取消下载
- ✅ Asset 去重（已缓存文件跳过）

### 技术债务
- ⬜ begin_version 中账户信息仍硬编码
- ⬜ account_page 缺少 [n]ew / [d]elete 功能
- ⬜ 版本安装不支持 Forge/Fabric
- ⬜ 没有 asset 文件校验
