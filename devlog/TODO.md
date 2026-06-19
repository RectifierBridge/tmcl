# TODO — 开发待办

## 当前阶段: P1 — 离线账户 CRUD ✅

- [x] account_init: 从 ~/.tmcl/accounts.json 加载，空列表则无账户
- [x] account_page: Enter 键触发 [n]ew / [d]elete 操作
- [x] 新建离线账户: popup 输入用户名 → 自动生成 UUID v3
- [x] 删除账户: 确认弹窗 → 从列表移除 → 持久化
- [x] 选择账户: 标记 selected → 持久化
- [x] account_write: 保存到 accounts.json
- [x] begin_version: 读取选中账户的 username/uuid/token

## 后续阶段

### P2 — 账户类型扩展 ✅

- [x] 账户类型选择器（新建时先选类型）
- [x] Microsoft / LittleSkin / Custom Yggdrasil 显示 "Coming soon"
- [x] AccountInfo 已包含所有后续需要的字段

### P3 — Microsoft 登录 ✅

- [x] OAuth2 device code flow（显示 microsoft.com/link + 验证码）
- [x] MS token → Xbox Live → XSTS → Minecraft token 链
- [x] 自动获取 Minecraft Profile（UUID + 用户名）
- [x] 账户信息持久化（含 access_token + refresh_token）

### P4 — LittleSkin 登录
- [ ] Yggdrasil API: /authserver/authenticate
- [ ] 邮箱 + 密码输入 UI

### P5 — 离线皮肤
- [ ] 皮肤文件路径存储
- [ ] 启动时注入皮肤路径参数

### P6 — 自定义 Yggdrasil ✅

- [x] 用户输入 Yggdrasil API 根 URL
- [x] 与 LittleSkin 复用同一认证流程（ygg_auth / ygg_login）
- [x] 多角色选择器
- [x] 启动时自动注入 authlib-injector

### P7 — Token 刷新
- [ ] Microsoft refresh_token 自动续期
- [ ] Yggdrasil 验证 + 刷新
