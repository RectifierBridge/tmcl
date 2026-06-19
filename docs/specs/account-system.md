# Account System — 功能规格

## 数据结构

```c
typedef struct {
  char username[32];       // 游戏内显示名称
  char uuid[64];           // Minecraft UUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
  char type[16];           // offline / microsoft / littleskin / third_party
  char access_token[512];  // OAuth2 / Yggdrasil access token
  char refresh_token[512]; // Microsoft refresh token (仅 microsoft)
  char auth_server[256];   // 第三方 Yggdrasil API 根 URL (仅 third_party)
  char skin_path[256];     // PNG 皮肤文件路径 (仅 offline, 64x64)
  int selected;            // 当前选中账户
} AccountInfo;
```

## 存储

- 路径: `~/.tmcl/accounts.json`
- 格式: cJSON 数组

## UUID v3 生成 (离线账户)

- Namespace: DNS (`6ba7b810-9dad-11d1-80b4-00c04fd430c8`)
- Name: `OfflinePlayer:<username>`
- 算法: MD5 → UUID 格式化

## 账户类型

| 类型 | 登录方式 | Token 来源 |
|------|---------|-----------|
| offline | 仅用户名 | 无需 token |
| microsoft | OAuth2 device code | Xbox Live 链 |
| littleskin | 邮箱 + 密码 | Yggdrasil /authserver/authenticate |
| third_party | 邮箱 + 密码 | 自定义 Yggdrasil 服务器 |

## 登录流程

### Microsoft
1. POST device code → 显示 microsoft.com/link + 用户码
2. 用户在手机上授权
3. 轮询 token endpoint
4. MS token → Xbox Live → XSTS → Minecraft token
5. GET Minecraft profile → UUID + username

### LittleSkin
1. 用户输入邮箱 + 密码
2. POST /authserver/authenticate → access_token + profiles
3. 用户选择角色 → 存储 UUID + username + token

## 启动时使用

`begin_version()` 读取选中账户:
- `--username` → account.username
- `--uuid` → account.uuid
- `--accessToken` → account.access_token (offline 用 "0")
- `--userType` → microsoft 用 "msa"，其他用 "mojang"
