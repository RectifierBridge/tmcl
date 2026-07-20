## Microsoft 正版登录 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 account.c 中实现 Microsoft Device Code OAuth 认证完整流程，替换 "Coming soon" 占位。

**Architecture:** 在现有 account.c 中新增 5 个静态辅助函数，修改 2 个现有函数。所有新增函数使用已有的 curl_fetch() 进行 HTTP 请求。交互使用阻塞等待（step 展示验证码 → 轮询 → 成功/失败）。

**Tech Stack:** C, ncurses, cjson, curl (popen), pthread (已有)

---

### Task 1: 添加常量与辅助函数声明

**Files:**
- Modify: `src/account/account.c:1-10`

- [ ] **Step 1: 在 include 区域后添加宏定义**

在 `#include <unistd.h>` 之后、`curl_fetch` 之前插入：

```c
/* ── Microsoft OAuth constants ── */
#define MICROSOFT_CLIENT_ID "00000000402b5328"
#define MS_DEVICECODE_URL \
  "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"
#define MS_TOKEN_URL \
  "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"
#define MS_XBL_URL "https://user.auth.xboxlive.com/user/authenticate"
#define MS_XSTS_URL "https://xsts.auth.xboxlive.com/xsts/authorize"
#define MS_MC_LOGIN_URL \
  "https://api.minecraftservices.com/authentication/login_with_xbox"
#define MS_ENTITLEMENTS_URL \
  "https://api.minecraftservices.com/entitlements/mcstore"
#define MS_PROFILE_URL "https://api.minecraftservices.com/minecraft/profile"
#define MS_SCOPE "XboxLive.signin offline_access"
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功（此刻只添加了宏，无新增函数调用）。

---

### Task 2: 实现 ms_device_code_req (Step 1)

**Files:**
- Modify: `src/account/account.c` — 在 `ms_cleanup()` 函数之后插入

- [ ] **Step 1: 实现函数**

```c
/* 请求 Device Code，返回 device_code（调用者 free）。
 * 同时通过输出参数返回 user_code, verification_uri, interval, expires_in */
static char *ms_device_code_req(char **user_code_out, char **verify_uri_out,
                                int *interval_out, int *expires_out) {
  char *post = NULL;
  asprintf(&post, "client_id=%s&scope=%s", MICROSOFT_CLIENT_ID, MS_SCOPE);
  if (!post) return NULL;
  char *resp = curl_fetch(MS_DEVICECODE_URL, post, NULL,
                          "application/x-www-form-urlencoded");
  free(post);
  if (!resp) return NULL;

  cJSON *j = cJSON_Parse(resp);
  free(resp);
  if (!j) return NULL;

  cJSON *dc = cJSON_GetObjectItem(j, "device_code");
  cJSON *uc = cJSON_GetObjectItem(j, "user_code");
  cJSON *vu = cJSON_GetObjectItem(j, "verification_uri");
  cJSON *it = cJSON_GetObjectItem(j, "interval");
  cJSON *ex = cJSON_GetObjectItem(j, "expires_in");

  char *device_code = NULL;
  if (dc && uc && vu && it && ex &&
      cJSON_IsString(dc) && cJSON_IsString(uc) && cJSON_IsString(vu)) {
    device_code = strdup(dc->valuestring);
    *user_code_out = strdup(uc->valuestring);
    *verify_uri_out = strdup(vu->valuestring);
    *interval_out = it->valueint;
    *expires_out = ex->valueint;
  }
  cJSON_Delete(j);
  return device_code;
}
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 3: 实现 ms_poll_token (Step 3)

**Files:**
- Modify: `src/account/account.c` — 在 `ms_device_code_req` 之后

- [ ] **Step 1: 实现轮询函数**

```c
/* 轮询 MS token 端点，直到用户完成授权或超时/取消。
 * 返回 access_token（调用者 free），通过 refresh_token_out 返回 refresh_token。
 * can_cancel: 指向调用者可控的取消标志，q 键按下时设为 1 */
static char *ms_poll_token(const char *device_code, int interval_sec,
                           int expires_sec, char **refresh_token_out,
                           volatile int *can_cancel) {
  char *post_tpl = NULL;
  asprintf(&post_tpl,
           "grant_type=urn:ietf:params:oauth:grant-type:device_code"
           "&client_id=%s&device_code=%%s",
           MICROSOFT_CLIENT_ID);
  if (!post_tpl) return NULL;

  int elapsed = 0;
  int attempts = 0;
  int row, col;
  getmaxyx(stdscr, row, col);

  while (elapsed < expires_sec) {
    /* 检查取消 */
    if (can_cancel && *can_cancel) { free(post_tpl); return NULL; }

    /* 展示轮询状态 */
    mvprintw(row - 2, 4,
             "Checking... [%d attempts, %ds elapsed]  Press q to cancel",
             attempts, elapsed);
    refresh();

    /* 等待 interval 秒，但每秒检查一次取消 */
    for (int w = 0; w < interval_sec; w++) {
      sleep(1);
      elapsed++;
      if (can_cancel && *can_cancel) { free(post_tpl); return NULL; }
      if (elapsed >= expires_sec) break;
    }

    char *post = NULL;
    asprintf(&post, post_tpl, device_code);
    if (!post) { free(post_tpl); return NULL; }

    char *resp = curl_fetch(MS_TOKEN_URL, post, NULL,
                            "application/x-www-form-urlencoded");
    free(post);
    attempts++;

    if (!resp) continue; /* 网络错误，重试 */

    cJSON *j = cJSON_Parse(resp);
    free(resp);
    if (!j) continue;

    cJSON *at = cJSON_GetObjectItem(j, "access_token");
    cJSON *rt = cJSON_GetObjectItem(j, "refresh_token");

    if (at && cJSON_IsString(at)) {
      char *access = strdup(at->valuestring);
      if (rt && cJSON_IsString(rt))
        *refresh_token_out = strdup(rt->valuestring);
      cJSON_Delete(j);
      free(post_tpl);
      return access;
    }

    /* 检查错误 */
    cJSON *err = cJSON_GetObjectItem(j, "error");
    if (err && cJSON_IsString(err)) {
      if (strcmp(err->valuestring, "authorization_pending") != 0) {
        /* 非 pending 错误 → 失败 */
        if (strcmp(err->valuestring, "authorization_declined") == 0 ||
            strcmp(err->valuestring, "expired_token") == 0 ||
            strcmp(err->valuestring, "bad_verification_code") == 0) {
          cJSON_Delete(j);
          free(post_tpl);
          return strdup(err->valuestring);
          /* 调用者通过前缀判断：无 "eyJ" 开头 = 错误字符串 */
        }
      }
    }
    cJSON_Delete(j);

    /* 展示提示 */
    clear();
  }

  free(post_tpl);
  return NULL; /* 超时 */
}
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 4: 实现 ms_check_ownership (Step 7)

**Files:**
- Modify: `src/account/account.c` — 在 `ms_poll_token` 之后

- [ ] **Step 1: 实现拥有权检查**

```c
/* 检查 Minecraft 正版拥有权。返回 1=拥有，0=未拥有 */
static int ms_check_ownership(const char *mc_token) {
  char *auth = NULL;
  asprintf(&auth, "Authorization: Bearer %s", mc_token);
  if (!auth) return 0;

  char *resp = curl_fetch(MS_ENTITLEMENTS_URL, NULL, auth, "application/json");
  free(auth);
  if (!resp) return 0;

  cJSON *j = cJSON_Parse(resp);
  free(resp);
  if (!j) return 0;

  int owns = 0;
  cJSON *items = cJSON_GetObjectItem(j, "items");
  if (items && cJSON_IsArray(items)) {
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n; i++) {
      cJSON *item = cJSON_GetArrayItem(items, i);
      cJSON *name = cJSON_GetObjectItem(item, "name");
      if (name && cJSON_IsString(name) &&
          (strcmp(name->valuestring, "product_minecraft") == 0 ||
           strcmp(name->valuestring, "game_minecraft") == 0)) {
        owns = 1;
        break;
      }
    }
  }
  cJSON_Delete(j);
  return owns;
}
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 5: 实现 ms_full_login (串联 Steps 1-8)

**Files:**
- Modify: `src/account/account.c` — 在 `ms_check_ownership` 之后

- [ ] **Step 1: 实现完整登录流程函数**

```c
/* Microsoft 完整登录流程，在 ncurses TUI 中运行。
 * 成功返回 1，填充 name, uuid, mc_token, refresh_token。
 * 失败/取消返回 0。 */
static int ms_full_login(char *name_out, size_t name_sz,
                         char *uuid_out, size_t uuid_sz,
                         char *mc_token_out, size_t mt_sz,
                         char *refresh_token_out, size_t rt_sz) {
  int row, col;
  getmaxyx(stdscr, row, col);
  volatile int cancel = 0;

  /* ── Step 1: 获取 device code ── */
  clear();
  mvprintw(3, 4, "Microsoft Login");
  mvprintw(5, 4, "Requesting device code...");
  refresh();

  char *user_code = NULL, *verify_uri = NULL;
  int interval = 5, expires = 900;
  char *device_code = ms_device_code_req(&user_code, &verify_uri,
                                         &interval, &expires);
  if (!device_code) {
    mvprintw(7, 4, "Cannot connect to Microsoft. Check your network.");
    mvprintw(9, 4, "Press any key to return...");
    refresh(); getch(); clear();
    return 0;
  }

  /* ── Step 2: 展示验证码 ── */
  clear();
  mvprintw(3, 4, "Microsoft Login");
  mvprintw(5, 4, "Please open:  https://microsoft.com/link");
  mvprintw(6, 4, "Enter code:    %s", user_code);
  mvprintw(9, 4, "Press q to cancel");
  refresh();

  /* 启动轮询的同时监听 q 键 */
  /* 使用非阻塞 getch 检查取消 */
  nodelay(stdscr, TRUE);

  /* ── Step 3: 轮询 token ── */
  char *access_token = NULL, *refresh_token = NULL;
  int poll_elapsed = 0, poll_attempts = 0;

  while (poll_elapsed < expires) {
    int c = getch();
    if (c == 'q' || c == 'Q') { cancel = 1; break; }

    /* 每秒轮询一次（比 interval 更细粒度以响应取消） */
    sleep(1);
    poll_elapsed++;

    /* 每 interval 秒发一次请求 */
    if (poll_elapsed % interval != 0) continue;

    char *post = NULL;
    asprintf(&post,
             "grant_type=urn:ietf:params:oauth:grant-type:device_code"
             "&client_id=%s&device_code=%s",
             MICROSOFT_CLIENT_ID, device_code);
    if (!post) break;

    char *resp = curl_fetch(MS_TOKEN_URL, post, NULL,
                            "application/x-www-form-urlencoded");
    free(post);
    poll_attempts++;

    if (!resp) {
      mvprintw(row - 2, 4,
               "Checking... [%d attempts, %ds elapsed]",
               poll_attempts, poll_elapsed);
      refresh();
      continue;
    }

    cJSON *j = cJSON_Parse(resp);
    free(resp);
    if (!j) continue;

    cJSON *at = cJSON_GetObjectItem(j, "access_token");
    cJSON *rt = cJSON_GetObjectItem(j, "refresh_token");

    if (at && cJSON_IsString(at)) {
      access_token = strdup(at->valuestring);
      if (rt && cJSON_IsString(rt))
        refresh_token = strdup(rt->valuestring);
      cJSON_Delete(j);
      break;
    }

    cJSON *err = cJSON_GetObjectItem(j, "error");
    if (err && cJSON_IsString(err)) {
      if (strcmp(err->valuestring, "authorization_pending") != 0) {
        /* 失败 */
        mvprintw(row - 2, 4,
                 "Authorization failed: %s", err->valuestring);
        mvprintw(row - 1, 4, "Press any key to return...");
        cJSON_Delete(j);
        free(device_code); free(user_code); free(verify_uri);
        nodelay(stdscr, FALSE);
        refresh(); getch(); clear();
        return 0;
      }
    }
    cJSON_Delete(j);

    mvprintw(row - 2, 4,
             "Checking... [%d attempts, %ds elapsed]",
             poll_attempts, poll_elapsed);
    refresh();
  }

  nodelay(stdscr, FALSE);
  free(device_code); free(user_code); free(verify_uri);

  if (cancel || !access_token) {
    if (!cancel)
      mvprintw(row - 1, 4, "Authorization timed out.");
    mvprintw(row, 4, "Press any key to return...");
    refresh(); getch(); clear();
    free(access_token); free(refresh_token);
    return 0;
  }

  /* ── Step 4-6: XBL → XSTS → MC ── */
  mvprintw(row - 6, 4, "Approved. Authenticating with Xbox Live...");
  refresh();

  char *xbl_token = ms_xbox_auth(access_token);
  if (!xbl_token) {
    mvprintw(row - 4, 4, "Xbox Live authentication failed.");
    goto fail_with_at;
  }

  mvprintw(row - 6, 4, "Approved. Authenticating with XSTS...");
  refresh();

  char *xsts_json = ms_xsts_auth(xbl_token);
  free(xbl_token);
  if (!xsts_json) {
    mvprintw(row - 4, 4, "XSTS authorization failed.");
    goto fail_with_at;
  }

  /* 检查 XErr */
  cJSON *xsts_j = cJSON_Parse(xsts_json);
  if (xsts_j) {
    cJSON *xerr = cJSON_GetObjectItem(xsts_j, "XErr");
    if (xerr) {
      long code = xerr->valuedouble;
      const char *msg = "Unknown Xbox error.";
      if (code == 2148916227) msg = "Account is banned from Xbox.";
      else if (code == 2148916233) msg = "No Xbox account. Sign up at xbox.com.";
      else if (code == 2148916238) msg = "Child account needs parental consent.";
      mvprintw(row - 4, 4, "%s (XErr: %ld)", msg, code);
      cJSON_Delete(xsts_j); free(xsts_json);
      goto fail_with_at;
    }
    cJSON_Delete(xsts_j);
  }

  mvprintw(row - 6, 4, "Approved. Authenticating with Minecraft...");
  refresh();

  char *mc_token = ms_mc_login(xsts_json);
  free(xsts_json);
  if (!mc_token) {
    mvprintw(row - 4, 4, "Minecraft authentication failed.");
    goto fail_with_at;
  }

  /* ── Step 7: 拥有权检查 ── */
  mvprintw(row - 6, 4, "Approved. Checking game license...");
  refresh();

  if (!ms_check_ownership(mc_token)) {
    mvprintw(row - 4, 4,
             "This Microsoft account does not own Minecraft Java Edition.");
    free(mc_token);
    goto fail_with_at;
  }

  /* ── Step 8: Profile ── */
  mvprintw(row - 6, 4, "Approved. Loading profile...");
  refresh();

  char ms_uuid[64] = {0}, ms_name[32] = {0};
  if (!ms_profile(mc_token, ms_uuid, sizeof(ms_uuid),
                  ms_name, sizeof(ms_name))) {
    mvprintw(row - 4, 4,
             "No Minecraft profile. If using Game Pass, log into the official"
             " launcher first.");
    free(mc_token);
    goto fail_with_at;
  }

  /* 成功 */
  mvprintw(row - 6, 4, "Profile loaded: %s", ms_name);
  mvprintw(row - 4, 4, "Login successful!");
  mvprintw(row - 2, 4, "Press any key to return...");
  refresh(); getch(); clear();

  strncpy(name_out, ms_name, name_sz - 1); name_out[name_sz - 1] = '\0';
  strncpy(uuid_out, ms_uuid, uuid_sz - 1); uuid_out[uuid_sz - 1] = '\0';
  strncpy(mc_token_out, mc_token, mt_sz - 1); mc_token_out[mt_sz - 1] = '\0';
  if (refresh_token) {
    strncpy(refresh_token_out, refresh_token, rt_sz - 1);
    refresh_token_out[rt_sz - 1] = '\0';
    free(refresh_token);
  }
  free(access_token);
  free(mc_token);
  return 1;

fail_with_at:
  free(access_token);
  free(refresh_token);
  mvprintw(row - 2, 4, "Press any key to return...");
  refresh(); getch(); clear();
  return 0;
}
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 6: 替换 new_account_popup 中的 Microsoft 占位

**Files:**
- Modify: `src/account/account.c` — 找到 `if (sel == 1) { /* Microsoft — Coming soon! */ }` 区块

- [ ] **Step 1: 删除占位代码，替换为 ms_full_login 调用**

将：
```c
  if (sel == 1) {
    // Microsoft — 待后续开发
    clear();
    mvprintw(5, 4, "Microsoft Login");
    mvprintw(7, 4, "Coming soon!");
    mvprintw(9, 4, "Press any key to return...");
    getch();
    clear();
    return;
  }
```

替换为：
```c
  if (sel == 1) {
    // Microsoft Device Code 登录
    char ms_uuid[64] = {0}, ms_name[32] = {0};
    char ms_mc_token[512] = {0}, ms_refresh[512] = {0};

    if (!ms_full_login(ms_name, sizeof(ms_name),
                       ms_uuid, sizeof(ms_uuid),
                       ms_mc_token, sizeof(ms_mc_token),
                       ms_refresh, sizeof(ms_refresh))) {
      return; // 失败或取消，ms_full_login 已展示错误并等待按键
    }

    // 创建账户
    state->account_count++;
    state->accounts = realloc(state->accounts,
                              state->account_count * sizeof(AccountInfo));
    AccountInfo *a = &state->accounts[state->account_count - 1];
    memset(a, 0, sizeof(*a));
    strncpy(a->username, ms_name, sizeof(a->username) - 1);
    strncpy(a->uuid, ms_uuid, sizeof(a->uuid) - 1);
    strncpy(a->type, "microsoft", sizeof(a->type) - 1);
    strncpy(a->access_token, ms_mc_token, sizeof(a->access_token) - 1);
    strncpy(a->refresh_token, ms_refresh, sizeof(a->refresh_token) - 1);
    a->auth_server[0] = '\0';
    a->selected = 0;
    if (state->account_count == 1) {
      a->selected = 1;
      state->selected_account = 0;
    }
    account_write(state);
    clear();
    return;
  }
```

- [ ] **Step 2: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 7: 实现 ms_refresh 并修改 account_refresh_token

**Files:**
- Modify: `src/account/account.c` — 在 `ms_full_login` 之后添加 `ms_refresh`；修改 `account_refresh_token`

- [ ] **Step 1: 添加 ms_refresh 函数**

```c
/* 用 Microsoft refresh_token 刷新 Minecraft access_token。
 * 成功返回 1，刷新后的 token 写入 mc_token_out。失败返回 0。 */
static int ms_refresh(const char *refresh_token, char *mc_token_out,
                      size_t mc_token_sz) {
  /* 1. 用 refresh_token 换新的 access_token */
  char *post = NULL;
  asprintf(&post,
           "grant_type=refresh_token&client_id=%s&refresh_token=%s"
           "&scope=%s",
           MICROSOFT_CLIENT_ID, refresh_token, MS_SCOPE);
  if (!post) return 0;

  char *resp = curl_fetch(MS_TOKEN_URL, post, NULL,
                          "application/x-www-form-urlencoded");
  free(post);
  if (!resp) return 0;

  cJSON *j = cJSON_Parse(resp);
  free(resp);
  if (!j) return 0;

  cJSON *at = cJSON_GetObjectItem(j, "access_token");
  if (!at || !cJSON_IsString(at)) { cJSON_Delete(j); return 0; }
  char *ms_access = strdup(at->valuestring);
  cJSON_Delete(j);

  /* 2. XBL → XSTS → MC（与之前的 ms_xbox_auth/ms_xsts_auth/ms_mc_login 相同） */
  char *xbl_token = ms_xbox_auth(ms_access);
  free(ms_access);
  if (!xbl_token) return 0;

  char *xsts_json = ms_xsts_auth(xbl_token);
  free(xbl_token);
  if (!xsts_json) return 0;

  char *mc_token = ms_mc_login(xsts_json);
  free(xsts_json);
  if (!mc_token) return 0;

  strncpy(mc_token_out, mc_token, mc_token_sz - 1);
  mc_token_out[mc_token_sz - 1] = '\0';
  free(mc_token);
  return 1;
}
```

- [ ] **Step 2: 修改 account_refresh_token 添加 Microsoft 分发**

找到 `account_refresh_token` 函数（当前实现仅调用 `ygg_refresh_token`）：

```c
void account_refresh_token(AccountInfo *a) { ygg_refresh_token(a); }
```

替换为：

```c
void account_refresh_token(AccountInfo *a) {
  if (!a || !a->access_token[0]) return;

  if (strcmp(a->type, "microsoft") == 0) {
    /* Microsoft 刷新 */
    if (!a->refresh_token[0]) return;
    char new_token[512] = {0};
    if (ms_refresh(a->refresh_token, new_token, sizeof(new_token))) {
      strncpy(a->access_token, new_token, sizeof(a->access_token) - 1);
      a->access_token[sizeof(a->access_token) - 1] = '\0';
      account_write(g_account_state);
    }
  } else {
    ygg_refresh_token(a);
  }
}
```

- [ ] **Step 3: 编译验证**

```bash
make clean && make
```

预期：编译成功。

---

### Task 8: 端到端测试

- [ ] **Step 1: 编译完全清理后再构建**

```bash
make clean && make
```

预期：零错误，零 warning（除已有 warning）。

- [ ] **Step 2: 启动 tmcl，导航到 Account 页面**

```bash
./tmcl
```

- [ ] **Step 3: 测试 Microsoft 登录流程**

按 `A` → 选中 Account 页面 `[A]ccount`，按 `n` → 选择 `Microsoft` → 验证以下步骤：
1. 界面显示 `microsoft.com/link` 和验证码
2. 在浏览器中输入验证码完成授权
3. tmcl 检测到授权并逐步推进
4. 最终显示 "Login successful!" 和用户名
5. 账户出现在列表中，`type` 为 `microsoft`

- [ ] **Step 4: 验证存储**

```bash
cat ~/.tmcl/accounts.json
```

预期：新条目中 `type` 为 `"microsoft"`，`username` 和 `uuid` 已填充，`access_token` 和 `refresh_token` 非空。

- [ ] **Step 5: 验证取消流程**

在展示验证码后按 `q` → 预期回到账户页面，无新账户添加。

- [ ] **Step 6: 验证版本启动**

选择 Microsoft 账户 → 切换到 Version 页面 → 启动任意已安装版本 → 预期游戏正常启动，使用正版账户。

- [ ] **Step 7: 提交**

```bash
git add src/account/account.c
git add docs/superpowers/specs/2026-07-20-microsoft-auth-design.md
git add docs/superpowers/plans/2026-07-20-microsoft-auth.md
git commit -m "feat: Microsoft 正版登录 (Device Code OAuth)"
```
