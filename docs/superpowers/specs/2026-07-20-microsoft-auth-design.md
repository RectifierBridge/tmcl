## Microsoft 正版登录 — 设计文档

### 概述

在 tmcl 中实现 Microsoft 账户的完整登录流程，使用 OAuth 2.0 Device Code 流，通过 XBL → XSTS → Minecraft → Profile 链式认证，支持 token 刷新。

### Source of Truth

- [Minecraft Wiki — Microsoft Authentication](https://minecraft.wiki/w/Microsoft_Authentication)
- [Minecraft-Authenticator (Java)](https://github.com/Hycrafthd/Minecraft-Authenticator) — 参考 `reference/` 目录
- [PythonMinecraftLauncher minecraft_auth.py](https://github.com/.../PythonMinecraftLauncher) — 参考 `reference/` 目录
- [HMCL (Hello Minecraft! Launcher)](https://github.com/HMCL-dev/HMCL) — 参考 `reference/` 目录

### Client ID

使用 Minecraft 官方启动器的 Azure Client ID：`00000000402b5328`。
以 `#define MICROSOFT_CLIENT_ID` 在 account.c 中定义。
此 ID 不会被 2148916238（儿童账户未加入家庭组）错误影响。

### 认证流程 (Device Code)

```
Step 1. POST https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode
        Content-Type: application/x-www-form-urlencoded
        client_id=<CLIENT_ID>&scope=XboxLive.signin offline_access
        → user_code, device_code, verification_uri, expires_in, interval

Step 2. 用户访问 microsoft.com/link 输入 user_code

Step 3. 轮询 POST https://login.microsoftonline.com/consumers/oauth2/v2.0/token
        Content-Type: application/x-www-form-urlencoded
        grant_type=urn:ietf:params:oauth:grant-type:device_code
        &client_id=<CLIENT_ID>&device_code=<DEVICE_CODE>
        → access_token, refresh_token

Step 4. XBL 认证 (已有 ms_xbox_auth)
        POST https://user.auth.xboxlive.com/user/authenticate
        → XBL token + uhs

Step 5. XSTS 授权 (已有 ms_xsts_auth)
        POST https://xsts.auth.xboxlive.com/xsts/authorize
        → XSTS token + uhs

Step 6. Minecraft 登录 (已有 ms_mc_login)
        POST https://api.minecraftservices.com/authentication/login_with_xbox
        → Minecraft access_token

Step 7. 检查拥有权 (新增 ms_check_ownership)
        GET https://api.minecraftservices.com/entitlements/mcstore
        Authorization: Bearer <MC_TOKEN>
        → 检查 items 中是否包含 product_minecraft 或 game_minecraft

Step 8. 获取 Profile (已有 ms_profile)
        GET https://api.minecraftservices.com/minecraft/profile
        Authorization: Bearer <MC_TOKEN>
        → id (UUID), name (用户名)
```

### 代码结构

修改文件：`src/account/account.c`
头文件：来自 `src/account/account.h`（现有结构体无需修改）

**新增函数：**

| 函数 | 签名 | 职责 |
|------|------|------|
| `ms_device_code_req` | `static char *ms_device_code_req(char **user_code, char **verification_uri, int *interval, int *expires_in)` | Step 1，返回 device_code |
| `ms_poll_token` | `static char *ms_poll_token(const char *device_code, int interval, int expires_in, char **refresh_token)` | Step 3，阻塞轮询，返回 access_token |
| `ms_check_ownership` | `static int ms_check_ownership(const char *mc_token)` | Step 7，检查拥有权 |
| `ms_full_login` | `static int ms_full_login(char *name_out, size_t name_sz, char *uuid_out, size_t uuid_sz, char *mc_token_out, size_t mt_sz, char *refresh_token_out, size_t rt_sz)` | 串联 Steps 1-8 的完整流程 |
| `ms_refresh` | `static int ms_refresh(const char *refresh_token, char *mc_token_out, size_t msg)` | 刷新 Minecraft access token |

**修改的函数：**

| 函数 | 变更 |
|------|------|
| `new_account_popup` | Microsoft 分支替换为调用 `ms_full_login`，展示等待界面 |
| `account_refresh_token` | 根据 `type` 分发：`microsoft` → `ms_refresh`，其他 → `ygg_refresh_token` |

**现有函数不变：** `ms_xbox_auth`, `ms_xsts_auth`, `ms_mc_login`, `ms_profile`

### 交互体验

用户选择 Microsoft 后，进入阻塞等待界面：

```text
┌──────────────────────────────────────────┐
│ Microsoft Login                           │
│                                           │
│ Please open:  https://microsoft.com/link  │
│ Enter code:    ABCD1234                   │
│                                           │
│ Checking... [5 attempts, 25s elapsed]     │
│ Press q to cancel                         │
└──────────────────────────────────────────┘
```

成功后逐步更新状态提示：
```
✓ Microsoft account authorized
✓ Xbox Live authenticated
✓ Minecraft authenticated
✓ Game license verified
✓ Profile loaded: <username>
Press any key to return...
```

### 错误处理

| 阶段 | 错误 | 用户提示 |
|------|------|---------|
| Device Code 请求 | 网络错误/服务端拒绝 | "Cannot connect to Microsoft. Check your network." |
| 轮询 | 用户按 q | 中止，返回账户页 |
| 轮询 | 超时 (15分钟) | "Authorization timed out. Please try again." |
| 轮询 | authorization_declined | "Authorization was declined." |
| 轮询 | expired_token | "Verification code expired. Please restart." |
| XBL | HTTP != 200 | "Xbox Live authentication failed: <code>" |
| XSTS | XErr 2148916227 | "Account is banned from Xbox." |
| XSTS | XErr 2148916233 | "No Xbox account. Sign up at xbox.com first." |
| XSTS | XErr 2148916238 | "Child account needs parental consent." |
| Minecraft 登录 | HTTP != 200 | "Minecraft authentication failed." |
| 拥有权检查 | items 为空 | "This Microsoft account does not own Minecraft Java Edition." |
| Profile 获取 | 404 | "No Minecraft profile. If using Game Pass, log in to the official launcher once." |

### Token 刷新

```
POST https://login.microsoftonline.com/consumers/oauth2/v2.0/token
Content-Type: application/x-www-form-urlencoded
grant_type=refresh_token&client_id=<CID>&refresh_token=<RT>&scope=XboxLive.signin offline_access

→ access_token (新), refresh_token (可能更新)
→ 重新走 XBL → XSTS → MC 链
→ 更新 AccountInfo.access_token
```

### 存储格式

`~/.tmcl/accounts.json` 中新增的 Microsoft 账户条目：

```json
{
  "username": "PlayerName",
  "uuid": "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx",
  "type": "microsoft",
  "access_token": "eyJ... (Minecraft token)",
  "refresh_token": "M.R... (Microsoft refresh token)",
  "auth_server": "",
  "skin_path": "",
  "selected": true
}
```

`AccountInfo` 结构体无需修改，现有字段足够。

### 安全注意

- 所有 token 使用 HTTPS 端点传输
- refresh_token 存储在 `~/.tmcl/accounts.json` 中（权限 0644），该文件在用户家目录下
- 不在日志中记录任何 token
- curl 命令通过 `-s` 静默模式执行，避免 token 出现在进程列表中

### 兼容性

- 仅修改 `src/account/account.c`
- `AccountInfo` 和 `AccountState` 结构体不变
- 其他页面（version, config, home）不受影响
- 现有账户（offline, littleskin, third_party）登录流程完全不变

### 测试计划

1. `make clean && make` — 编译零错误
2. 启动 `./tmcl`，在 Account 页面选择 Microsoft → 验证 Device Code 流程
3. 在浏览器中输入验证码 → 验证轮询结束，认证链跑通
4. 检查 `~/.tmcl/accounts.json` — 确认 Microsoft 账户数据正确存储
5. 在 Version 页面选择该账户启动 Minecraft → 验证游戏正常启动
6. Token 过期后重新启动 → 验证 `account_refresh_token` 刷新流程
7. 失败场景：故意输错 → 验证错误提示
