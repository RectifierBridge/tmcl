# TMCL 功能需求

## 已实现

### 版本管理
- [x] 扫描 ~/.minecraft/versions/ 下的已安装版本
- [x] 显示 type / modloader / version / name
- [x] [b]egin 启动游戏
- [x] [p]in 固定版本到首页
- [x] [r]ename 重命名版本
- [x] [d]elete 删除版本
- [x] [n]ew 安装新版本（从云端下载）

### 游戏启动
- [x] 原版 Minecraft (net.minecraft.client.main.Main)
- [x] Forge 1.7.10 (net.minecraft.launchwrapper.Launch + Java 8)
- [x] Fabric 1.21+ (net.fabricmc.loader.impl.launch.knot.KnotClient)
- [x] classpath 自动构建（含继承链 + libraries）
- [x] JVM 参数从 version JSON 读取
- [x] 版本匹配 Java 自动选择 (javaVersion.majorVersion)

### 版本安装
- [x] type 选择 (release / snapshot / old_beta / old_alpha)
- [x] 从 version_manifest_v2.json 拉取版本列表
- [x] name 自定义（过滤特殊字符）
- [x] 多线程下载 (8 线程 libs, 96 线程 assets)
- [x] Ctrl+C 取消
- [x] 下载失败重试 2 次 + 失败总结
- [x] Asset 去重（跳过已缓存文件）

### 配置
- [x] java_path / memory / game_dir / jvm_args
- [x] download_source (Official / BMCLAPI)
- [x] mod_source (CurseForge / Modrinth)
- [x] threads (下载线程数，默认 96)
- [x] memory auto 模式 (70% 可用内存)
- [x] pinned_version / last_play (系统管理)

## 待实现

### 账户系统 (P1)
- [ ] 离线账户 CRUD
- [ ] UUID v3 生成
- [ ] 持久化到 accounts.json
- [ ] 启动时使用选中账户

### 账户系统 (P2-P7)
- [ ] Microsoft OAuth2 登录
- [ ] LittleSkin Yggdrasil 登录
- [ ] 自定义 Yggdrasil
- [ ] 离线皮肤
- [ ] Token 刷新

### 模组支持
- [ ] Mod 页面（浏览、启用/禁用）
- [ ] Forge/Fabric 版本安装

### 其他
- [ ] 中文等本地化
- [ ] 下载进度条（每个文件的实时进度）
- [ ] 文件校验 (SHA1)
