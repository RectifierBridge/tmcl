// account.c — 账户管理
#include "account.h"
#include <cjson/cJSON.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


// ======================== curl 辅助 ========================
// 执行 curl POST/GET，返回响应体（动态分配，调用者 free）
// content_type: "application/json" 或 "application/x-www-form-urlencoded"
static char *curl_fetch(const char *url, const char *post_data,
                        const char *auth_header, const char *content_type) {
  char cmd[8192];
  int off;
  if (post_data) {
    off = snprintf(cmd, sizeof(cmd),
                   "curl -s -L -H 'Content-Type: %s' %s--data-raw '%s' '%s'",
                   content_type,
                   auth_header ? "-H '" : "",
                   post_data, url);
    // Insert auth header if present (after -H)
    if (auth_header) {
      // Rebuild: put auth header between Content-Type and --data-raw
      off = snprintf(cmd, sizeof(cmd),
                     "curl -s -L -H 'Content-Type: %s' -H '%s' "
                     "--data-raw '%s' '%s'",
                     content_type, auth_header, post_data, url);
    }
  } else {
    if (auth_header) {
      off = snprintf(cmd, sizeof(cmd),
                     "curl -s -L -H 'Content-Type: %s' -H '%s' '%s'",
                     content_type, auth_header, url);
    } else {
      off = snprintf(cmd, sizeof(cmd),
                     "curl -s -L -H 'Content-Type: %s' '%s'",
                     content_type, url);
    }
  }
  if (off < 0 || off >= (int)sizeof(cmd)) return NULL;

  FILE *p = popen(cmd, "r");
  if (!p) return NULL;

  char *buf = NULL;
  size_t cap = 0, len = 0;
  char chunk[4096];
  while (fgets(chunk, sizeof(chunk), p)) {
    size_t cl = strlen(chunk);
    if (len + cl + 1 > cap) {
      cap = cap ? cap * 2 : 4096;
      buf = realloc(buf, cap);
    }
    memcpy(buf + len, chunk, cl);
    len += cl;
    buf[len] = '\0';
  }
  pclose(p);
  return buf;
}

// ======================== 离线 UUID 生成 (UUID v3) ========================
// 使用系统 md5sum 计算标准 Minecraft 离线 UUID：
// UUID.nameUUIDFromBytes(("OfflinePlayer:" + username).getBytes("UTF-8"))
static void offline_uuid(const char *username, char *out, size_t size) {
  char cmd[320];
  snprintf(cmd, sizeof(cmd), "echo -n 'OfflinePlayer:%s' | md5sum", username);
  FILE *p = popen(cmd, "r");
  if (!p) { snprintf(out, size, "00000000-0000-0000-0000-000000000000"); return; }
  char hash[33] = {0};
  fgets(hash, sizeof(hash), p);
  pclose(p);

  // md5sum 输出 32 字符 MD5 hex + 空格
  if (strlen(hash) < 32) {
    snprintf(out, size, "00000000-0000-0000-0000-000000000000");
    return;
  }

  // 解析为 16 字节，设置 UUID v3 version + variant bits
  unsigned char h[16];
  for (int i = 0; i < 16; i++) {
    unsigned int b;
    sscanf(hash + i * 2, "%02x", &b);
    h[i] = (unsigned char)b;
  }
  h[6] = (h[6] & 0x0f) | 0x30; // version 3
  h[8] = (h[8] & 0x3f) | 0x80; // variant

  snprintf(out, size,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7],
           h[8], h[9], h[10], h[11], h[12], h[13], h[14], h[15]);
}

// 从保存的 JSON 中提取字段
static char *js_get_str(cJSON *j, const char *key) {
  cJSON *v = cJSON_GetObjectItem(j, key);
  return (v && cJSON_IsString(v)) ? strdup(v->valuestring) : NULL;
}

// Step 3: Xbox Live 认证
static char *ms_xbox_auth(const char *ms_token) {
  char *post = NULL;
  asprintf(&post,
           "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
           "\"RpsTicket\":\"d=%s\"},\"RelyingParty\":\"http://auth.xboxlive.com\","
           "\"TokenType\":\"JWT\"}", ms_token);
  if (!post) return NULL;
  char *resp = curl_fetch("https://user.auth.xboxlive.com/user/authenticate",
                          post, NULL, "application/json");
  free(post);
  if (!resp) return NULL;
  cJSON *j = cJSON_Parse(resp); free(resp);
  if (!j) return NULL;
  char *token = js_get_str(j, "Token");
  cJSON_Delete(j);
  return token;
}

// Step 4: XSTS 认证 → 返回 {token, uhs} JSON
static char *ms_xsts_auth(const char *xbox_token) {
  char *post = NULL;
  asprintf(&post,
           "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
           "\"RelyingParty\":\"rp://api.minecraftservices.com/\","
           "\"TokenType\":\"JWT\"}", xbox_token);
  if (!post) return NULL;
  char *resp = curl_fetch("https://xsts.auth.xboxlive.com/xsts/authorize",
                          post, NULL, "application/json");
  free(post);
  if (!resp) return NULL;
  cJSON *j = cJSON_Parse(resp); free(resp);
  if (!j) return NULL;
  cJSON *token = cJSON_GetObjectItem(j, "Token");
  cJSON *dc = cJSON_GetObjectItem(j, "DisplayClaims");
  char *result = NULL;
  if (token && dc) {
    cJSON *xui_arr = cJSON_GetObjectItem(dc, "xui");
    cJSON *xui0 = cJSON_GetArrayItem(xui_arr, 0);
    cJSON *uhs = xui0 ? cJSON_GetObjectItem(xui0, "uhs") : NULL;
    if (uhs && cJSON_IsString(uhs)) {
      cJSON *save = cJSON_CreateObject();
      cJSON_AddStringToObject(save, "token", token->valuestring);
      cJSON_AddStringToObject(save, "uhs", uhs->valuestring);
      result = cJSON_PrintUnformatted(save);
      cJSON_Delete(save);
    }
  }
  cJSON_Delete(j);
  return result;
}

// Step 5: XSTS → Minecraft token
static char *ms_mc_login(const char *xsts_json) {
  cJSON *xj = cJSON_Parse(xsts_json);
  if (!xj) return NULL;
  char *xsts = js_get_str(xj, "token");
  char *uhs = js_get_str(xj, "uhs");
  cJSON_Delete(xj);
  if (!xsts || !uhs) { free(xsts); free(uhs); return NULL; }

  char *post = NULL;
  asprintf(&post, "{\"identityToken\":\"XBL3.0 x=%s;%s\"}", uhs, xsts);
  free(xsts); free(uhs);
  if (!post) return NULL;

  char *resp = curl_fetch(
      "https://api.minecraftservices.com/authentication/login_with_xbox", post,
      NULL, "application/json");
  free(post);
  if (!resp) return NULL;
  cJSON *j = cJSON_Parse(resp); free(resp);
  if (!j) return NULL;
  char *token = js_get_str(j, "access_token");
  cJSON_Delete(j);
  return token;
}

// Step 6: 获取 Minecraft Profile
static int ms_profile(const char *mc_token, char *uuid_out, size_t uuid_sz,
                      char *name_out, size_t name_sz) {
  char *ah = NULL;
  asprintf(&ah, "Authorization: Bearer %s", mc_token);
  if (!ah) return 0;
  char *resp = curl_fetch("https://api.minecraftservices.com/minecraft/profile",
                          NULL, ah, "application/json");
  free(ah);
  if (!resp) return 0;
  cJSON *j = cJSON_Parse(resp); free(resp);
  if (!j) return 0;
  cJSON *id = cJSON_GetObjectItem(j, "id");
  cJSON *name = cJSON_GetObjectItem(j, "name");
  int ok = 0;
  if (id && name && cJSON_IsString(id) && cJSON_IsString(name)) {
    strncpy(uuid_out, id->valuestring, uuid_sz - 1); uuid_out[uuid_sz-1]='\0';
    strncpy(name_out, name->valuestring, name_sz - 1); name_out[name_sz-1]='\0';
    ok = 1;
  }
  cJSON_Delete(j);
  return ok;
}

static void ms_cleanup() {}

// ======================== LittleSkin / Yggdrasil 登录 ========================
#define LITTLESKIN_ROOT "https://littleskin.cn/api/yggdrasil"
#define LITTLESKIN_AUTH LITTLESKIN_ROOT "/authserver/authenticate"

// 执行 Yggdrasil 认证，返回 JSON 响应（调用者 cJSON_Delete）
static cJSON *ygg_auth(const char *server, const char *email,
                        const char *password) {
  char *post = NULL;
  asprintf(&post,
           "{\"agent\":{\"name\":\"Minecraft\",\"version\":1},"
           "\"username\":\"%s\",\"password\":\"%s\"}", email, password);
  if (!post) return NULL;
  char *resp = curl_fetch(server, post, NULL, "application/json");
  free(post);
  if (!resp) return NULL;
  cJSON *j = cJSON_Parse(resp);
  free(resp);
  return j;
}

// LittleSkin / Yggdrasil 登录流程：返回 access_token + 选中的 profile
// 如果有多个 profile，让用户选择；单个则自动选择
static int ygg_login(const char *server, const char *email, const char *password,
                     char *uuid_out, size_t uuid_sz,
                     char *name_out, size_t name_sz,
                     char *token_out, size_t token_sz) {
  cJSON *j = ygg_auth(server, email, password);
  if (!j) return 0;

  cJSON *err = cJSON_GetObjectItem(j, "error");
  if (err) {
    cJSON_Delete(j);
    return 0; // 认证失败
  }

  cJSON *at = cJSON_GetObjectItem(j, "accessToken");
  cJSON *sp = cJSON_GetObjectItem(j, "selectedProfile");
  cJSON *ap = cJSON_GetObjectItem(j, "availableProfiles");

  if (!at || !cJSON_IsString(at)) { cJSON_Delete(j); return 0; }

  // 单个 profile 自动选择；多个则弹出选择器
  int count = ap ? cJSON_GetArraySize(ap) : 0;
  const char *uuid = NULL, *name = NULL;

  if (count == 0) {
    cJSON_Delete(j);
    return 0;
  } else if (count == 1) {
    cJSON *p = cJSON_GetArrayItem(ap, 0);
    cJSON *pid = cJSON_GetObjectItem(p, "id");
    cJSON *pname = cJSON_GetObjectItem(p, "name");
    uuid = pid && cJSON_IsString(pid) ? pid->valuestring : NULL;
    name = pname && cJSON_IsString(pname) ? pname->valuestring : NULL;
  } else {
    // 多 profile 选择器
    clear();
    mvprintw(3, 4, "Select a character:");
    int sel = 0;
    while (1) {
      int row, col;
      getmaxyx(stdscr, row, col);
      for (int i = 0; i < count; i++) {
        cJSON *p = cJSON_GetArrayItem(ap, i);
        cJSON *pn = cJSON_GetObjectItem(p, "name");
        if (i == sel) attron(A_REVERSE);
        mvprintw(5 + i, 6, "%s", pn && cJSON_IsString(pn) ? pn->valuestring : "?");
        if (i == sel) attroff(A_REVERSE);
      }
      mvprintw(7 + count, 4, "j/k: move  Enter: confirm");
      int c = getch();
      if (c == 'j' && sel < count - 1) sel++;
      if (c == 'k' && sel > 0) sel--;
      if (c == '\n') break;
    }
    cJSON *p = cJSON_GetArrayItem(ap, sel);
    cJSON *pid = cJSON_GetObjectItem(p, "id");
    cJSON *pname = cJSON_GetObjectItem(p, "name");
    uuid = pid && cJSON_IsString(pid) ? pid->valuestring : NULL;
    name = pname && cJSON_IsString(pname) ? pname->valuestring : NULL;
    clear();
  }

  if (!uuid || !name) { cJSON_Delete(j); return 0; }

  strncpy(uuid_out, uuid, uuid_sz - 1); uuid_out[uuid_sz - 1] = '\0';
  strncpy(name_out, name, name_sz - 1); name_out[name_sz - 1] = '\0';
  strncpy(token_out, at->valuestring, token_sz - 1); token_out[token_sz - 1] = '\0';
  cJSON_Delete(j);
  return 1;
}

// ---- Token 刷新（启动前调用，失败则保留旧 token）----
static void ygg_refresh_token(AccountInfo *a) {
  if (!a || !a->access_token[0] || !a->auth_server[0]) return;

  // Step 1: 验证当前 token
  char *vp = NULL;
  asprintf(&vp, "{\"accessToken\":\"%s\"}", a->access_token);
  if (!vp) return;
  char validate_url[512];
  snprintf(validate_url, sizeof(validate_url), "%s/authserver/validate",
           a->auth_server);
  char *v_resp = curl_fetch(validate_url, vp, NULL, "application/json");
  free(vp);
  if (v_resp) { free(v_resp); return; } // 204 No Content → valid, 无需刷新

  // Step 2: 刷新 token
  char *rp = NULL;
  asprintf(&rp,
           "{\"accessToken\":\"%s\",\"clientToken\":\"%s\"}",
           a->access_token, a->uuid);
  if (!rp) return;
  char refresh_url[512];
  snprintf(refresh_url, sizeof(refresh_url), "%s/authserver/refresh",
           a->auth_server);
  char *r_resp = curl_fetch(refresh_url, rp, NULL, "application/json");
  free(rp);
  if (!r_resp) return;

  cJSON *j = cJSON_Parse(r_resp);
  free(r_resp);
  if (!j) return;
  cJSON *at = cJSON_GetObjectItem(j, "accessToken");
  if (at && cJSON_IsString(at)) {
    strncpy(a->access_token, at->valuestring, sizeof(a->access_token) - 1);
    a->access_token[sizeof(a->access_token) - 1] = '\0';
  }
  cJSON_Delete(j);
}

// ======================== 账户管理 ========================

AccountState *g_account_state = NULL;

void account_init(AccountState *state, ConfigState *cstate) {
  g_account_state = state;
  snprintf(state->accounts_file, sizeof(state->accounts_file),
           "%s/.tmcl/accounts.json", cstate->home_dir);
  state->account_count = 0;
  state->accounts = NULL;
  state->selected_account = 0;
  state->scroll_offset = 0;

  FILE *f = fopen(state->accounts_file, "r");
  if (!f) return; // 没有账户文件 → 空列表

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(sz + 1);
  if (!data) { fclose(f); return; }
  fread(data, 1, sz, f);
  data[sz] = '\0';
  fclose(f);

  cJSON *arr = cJSON_Parse(data);
  free(data);
  if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(arr); return; }

  int count = cJSON_GetArraySize(arr);
  if (count == 0) { cJSON_Delete(arr); return; }

  state->accounts = malloc(count * sizeof(AccountInfo));
  if (!state->accounts) { cJSON_Delete(arr); return; }

  for (int i = 0; i < count; i++) {
    cJSON *obj = cJSON_GetArrayItem(arr, i);
    AccountInfo *a = &state->accounts[i];
    memset(a, 0, sizeof(*a));

    cJSON *j;
    j = cJSON_GetObjectItem(obj, "username"); if (j && cJSON_IsString(j))
      strncpy(a->username, j->valuestring, sizeof(a->username)-1);
    j = cJSON_GetObjectItem(obj, "uuid"); if (j && cJSON_IsString(j))
      strncpy(a->uuid, j->valuestring, sizeof(a->uuid)-1);
    j = cJSON_GetObjectItem(obj, "type"); if (j && cJSON_IsString(j))
      strncpy(a->type, j->valuestring, sizeof(a->type)-1);
    j = cJSON_GetObjectItem(obj, "access_token"); if (j && cJSON_IsString(j))
      strncpy(a->access_token, j->valuestring, sizeof(a->access_token)-1);
    j = cJSON_GetObjectItem(obj, "refresh_token"); if (j && cJSON_IsString(j))
      strncpy(a->refresh_token, j->valuestring, sizeof(a->refresh_token)-1);
    j = cJSON_GetObjectItem(obj, "auth_server"); if (j && cJSON_IsString(j))
      strncpy(a->auth_server, j->valuestring, sizeof(a->auth_server)-1);
    j = cJSON_GetObjectItem(obj, "skin_path"); if (j && cJSON_IsString(j))
      strncpy(a->skin_path, j->valuestring, sizeof(a->skin_path)-1);
    j = cJSON_GetObjectItem(obj, "selected"); a->selected = (j && cJSON_IsTrue(j));
    if (a->selected) state->selected_account = i;
  }
  state->account_count = count;
  cJSON_Delete(arr);
}

void account_write(AccountState *state) {
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < state->account_count; i++) {
    AccountInfo *a = &state->accounts[i];
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "username", a->username);
    cJSON_AddStringToObject(obj, "uuid", a->uuid);
    cJSON_AddStringToObject(obj, "type", a->type);
    cJSON_AddStringToObject(obj, "access_token", a->access_token);
    cJSON_AddStringToObject(obj, "refresh_token", a->refresh_token);
    cJSON_AddStringToObject(obj, "auth_server", a->auth_server);
    cJSON_AddStringToObject(obj, "skin_path", a->skin_path);
    cJSON_AddBoolToObject(obj, "selected", a->selected ? 1 : 0);
    cJSON_AddItemToArray(arr, obj);
  }
  char *json = cJSON_Print(arr);
  // 确保 ~/.tmcl/ 目录存在
  char dir[1100];
  snprintf(dir, sizeof(dir), "%s", state->accounts_file);
  char *slash = strrchr(dir, '/');
  if (slash) { *slash = '\0'; mkdir(dir, 0755); }
  FILE *f = fopen(state->accounts_file, "w");
  if (f) { fputs(json, f); fclose(f); }
  free(json);
  cJSON_Delete(arr);
}

void account_refresh_token(AccountInfo *a) { ygg_refresh_token(a); }

AccountInfo *account_get_selected(AccountState *state) {
  for (int i = 0; i < state->account_count; i++) {
    if (state->accounts[i].selected) return &state->accounts[i];
  }
  return NULL;
}

void account_cleanup(AccountState *state) {
  if (state->accounts) { free(state->accounts); state->accounts = NULL; }
  state->account_count = 0;
}

// ======================== 新建账户弹窗 ========================
static void new_account_popup(AccountState *state) {
  // ---- Step 1: 选择账户类型 ----
  const char *types[] = {"Offline", "Microsoft", "LittleSkin",
                          "Custom Yggdrasil"};
  const char *type_keys[] = {"offline", "microsoft", "littleskin",
                              "third_party"};
  int sel = 0;

  while (1) {
    clear();
    mvprintw(3, 4, "Select account type:");
    for (int i = 0; i < 4; i++) {
      if (i == sel) attron(A_REVERSE);
      mvprintw(5 + i, 6, "%s", types[i]);
      if (i == sel) attroff(A_REVERSE);
    }
    mvprintw(11, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' && sel < 3) sel++;
    if (c == 'k' && sel > 0) sel--;
    if (c == '\n') break;
    if (c == 'q') { clear(); return; }
  }

  // ---- Step 2: 非离线账户 ----
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

  if (sel == 2) {
    // ---- LittleSkin 登录 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 4, "LittleSkin Login");
    mvprintw(5, 4, "Enter email:");
    mvprintw(7, 4, "> ");
    char email[64] = {0};
    getnstr(email, sizeof(email) - 1);

    mvprintw(9, 4, "Enter password:");
    mvprintw(11, 4, "> ");
    noecho();
    char password[64] = {0};
    getnstr(password, sizeof(password) - 1);
    echo();
    noecho();
    curs_set(0);
    clear();

    if (email[0] == '\0' || password[0] == '\0') return;

    char ls_uuid[64] = {0}, ls_name[32] = {0}, ls_token[512] = {0};
    if (!ygg_login(LITTLESKIN_AUTH, email, password,
                   ls_uuid, sizeof(ls_uuid),
                   ls_name, sizeof(ls_name),
                   ls_token, sizeof(ls_token))) {
      clear();
      mvprintw(5, 4, "Login failed. Check email/password.");
      mvprintw(7, 4, "Press any key to return...");
      getch(); clear(); return;
    }

    // 创建账户
    state->account_count++;
    state->accounts = realloc(state->accounts,
                              state->account_count * sizeof(AccountInfo));
    AccountInfo *a = &state->accounts[state->account_count - 1];
    memset(a, 0, sizeof(*a));
    strncpy(a->username, ls_name, sizeof(a->username) - 1);
    strncpy(a->uuid, ls_uuid, sizeof(a->uuid) - 1);
    strncpy(a->type, type_keys[sel], sizeof(a->type) - 1);
    strncpy(a->access_token, ls_token, sizeof(a->access_token) - 1);
    strncpy(a->auth_server, LITTLESKIN_ROOT, sizeof(a->auth_server) - 1);
    a->selected = 0;
    if (state->account_count == 1) {
      a->selected = 1;
      state->selected_account = 0;
    }
    account_write(state);
    clear();
    return;
  }

  if (sel == 3) {
    // ---- Custom Yggdrasil 登录 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 4, "Custom Yggdrasil Login");
    mvprintw(5, 4, "Enter Yggdrasil API root URL:");
    mvprintw(6, 4, "(e.g. https://example.com/api/yggdrasil)");
    mvprintw(8, 4, "> ");
    char server_url[256] = {0};
    getnstr(server_url, sizeof(server_url) - 1);

    mvprintw(10, 4, "Enter email:");
    mvprintw(12, 4, "> ");
    char email[64] = {0};
    getnstr(email, sizeof(email) - 1);

    mvprintw(14, 4, "Enter password:");
    mvprintw(16, 4, "> ");
    noecho();
    char password[64] = {0};
    getnstr(password, sizeof(password) - 1);
    echo();
    noecho();
    curs_set(0);
    clear();

    if (server_url[0] == '\0' || email[0] == '\0' || password[0] == '\0')
      return;

    char yu_uuid[64] = {0}, yu_name[32] = {0}, yu_token[512] = {0};
    if (!ygg_login(server_url, email, password,
                   yu_uuid, sizeof(yu_uuid),
                   yu_name, sizeof(yu_name),
                   yu_token, sizeof(yu_token))) {
      clear();
      mvprintw(5, 4, "Login failed. Check URL/email/password.");
      mvprintw(7, 4, "Press any key to return...");
      getch(); clear(); return;
    }

    // 创建账户
    state->account_count++;
    state->accounts = realloc(state->accounts,
                              state->account_count * sizeof(AccountInfo));
    AccountInfo *a = &state->accounts[state->account_count - 1];
    memset(a, 0, sizeof(*a));
    strncpy(a->username, yu_name, sizeof(a->username) - 1);
    strncpy(a->uuid, yu_uuid, sizeof(a->uuid) - 1);
    strncpy(a->type, type_keys[sel], sizeof(a->type) - 1);
    strncpy(a->access_token, yu_token, sizeof(a->access_token) - 1);
    strncpy(a->auth_server, server_url, sizeof(a->auth_server) - 1);
    a->selected = 0;
    if (state->account_count == 1) {
      a->selected = 1;
      state->selected_account = 0;
    }
    account_write(state);
    clear();
    return;
  }

  // ---- Step 3: 离线账户 — 输入用户名 ----
  echo();
  curs_set(1);
  clear();
  mvprintw(3, 4, "New offline account");
  mvprintw(5, 4, "Enter username (letters, digits, _, -):");
  mvprintw(7, 4, "> ");

  char input[32] = {0};
  getnstr(input, sizeof(input) - 1);

  // 过滤非法字符
  char clean[32] = {0};
  int j = 0;
  for (int i = 0; input[i] && j < (int)sizeof(clean) - 1; i++) {
    char c = input[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      clean[j++] = c;
    }
  }
  clean[j] = '\0';

  noecho();
  curs_set(0);
  clear();

  if (clean[0] == '\0') return;

  // 检查重名
  for (int i = 0; i < state->account_count; i++) {
    if (strcmp(state->accounts[i].username, clean) == 0) {
      mvprintw(5, 4, "Account '%s' already exists!", clean);
      mvprintw(7, 4, "Press any key to return...");
      getch();
      clear();
      return;
    }
  }

  // 创建账户
  state->account_count++;
  state->accounts = realloc(state->accounts,
                            state->account_count * sizeof(AccountInfo));
  AccountInfo *a = &state->accounts[state->account_count - 1];
  memset(a, 0, sizeof(*a));

  strncpy(a->username, clean, sizeof(a->username) - 1);
  offline_uuid(clean, a->uuid, sizeof(a->uuid));
  strncpy(a->type, type_keys[sel], sizeof(a->type) - 1);
  a->selected = 0;

  if (state->account_count == 1) {
    a->selected = 1;
    state->selected_account = 0;
  }

  account_write(state);
}

// ======================== 删除账户弹窗 ========================
static void delete_account_popup(AccountState *state, int index) {
  if (index < 0 || index >= state->account_count) return;

  clear();
  int row, col;
  getmaxyx(stdscr, row, col);
  mvprintw(3, 4, "Delete account:");
  mvprintw(5, 4, "  %s (%s)", state->accounts[index].username,
           state->accounts[index].type);
  mvprintw(7, 4, "Are you sure? [y/N]");

  int c = getch();
  if (c != 'y' && c != 'Y') { clear(); return; }

  // 从数组中移除
  for (int i = index; i < state->account_count - 1; i++) {
    state->accounts[i] = state->accounts[i + 1];
  }
  state->account_count--;

  if (state->account_count == 0) {
    free(state->accounts);
    state->accounts = NULL;
    state->selected_account = 0;
  } else {
    state->accounts = realloc(state->accounts,
                              state->account_count * sizeof(AccountInfo));
    if (index <= state->selected_account && state->selected_account > 0) {
      state->selected_account--;
    }
    // 如果删除的是选中账户，选第一个
    if (state->accounts[state->selected_account].selected == 0) {
      int found = 0;
      for (int i = 0; i < state->account_count; i++) {
        if (state->accounts[i].selected) { found = 1; break; }
      }
      if (!found && state->account_count > 0) {
        state->accounts[0].selected = 1;
        state->selected_account = 0;
      }
    }
  }

  account_write(state);
  clear();
}

// ======================== 皮肤设置弹窗 ========================
static void set_skin_popup(AccountInfo *a) {
  echo();
  curs_set(1);
  clear();
  mvprintw(3, 4, "Set offline skin");
  mvprintw(5, 4, "Current: %s",
           a->skin_path[0] ? a->skin_path : "(none)");
  mvprintw(7, 4, "Enter path to PNG skin file (64x64):");
  mvprintw(9, 4, "Requires CustomSkinLoader mod to work.");
  mvprintw(10, 4, "(empty to clear / keep current)");
  mvprintw(12, 4, "> ");

  char input[256] = {0};
  getnstr(input, sizeof(input) - 1);
  if (input[0] != '\0') {
    strncpy(a->skin_path, input, sizeof(a->skin_path) - 1);
    a->skin_path[sizeof(a->skin_path) - 1] = '\0';
  }
  noecho();
  curs_set(0);
  clear();
}

// ======================== 账户页面 ========================
void account_page(int ch, int *middlep, AccountState *state) {
  clear();
  int row = 0, col = 0;
  getmaxyx(stdscr, row, col);

  // 处理输入
  switch (ch) {
  case 'j':
    if (state->selected_account < state->account_count - 1) {
      state->selected_account++;
      int visible = row - 10;
      if (state->selected_account >= state->scroll_offset + visible)
        state->scroll_offset++;
    }
    break;
  case 'k':
    if (state->selected_account > 0) {
      state->selected_account--;
      if (state->selected_account < state->scroll_offset)
        state->scroll_offset--;
    }
    break;
  case 'c': // choose
    if (state->selected_account >= 0 &&
        state->selected_account < state->account_count) {
      for (int i = 0; i < state->account_count; i++)
        state->accounts[i].selected = 0;
      state->accounts[state->selected_account].selected = 1;
      account_write(state);
    }
    *middlep = 0;
    break;
  case 'n': // new
    new_account_popup(state);
    *middlep = 1;
    break;
  case 'd': // delete
    delete_account_popup(state, state->selected_account);
    *middlep = 2;
    break;
  case 's': // skin（仅离线账户有效）
    if (state->selected_account >= 0 &&
        state->selected_account < state->account_count &&
        strcmp(state->accounts[state->selected_account].type, "offline") == 0) {
      set_skin_popup(&state->accounts[state->selected_account]);
      account_write(state);
    }
    *middlep = 3;
    break;
  case 'h':
    *middlep = (*middlep + 3) % 4;
    break;
  case 'l':
    *middlep = (*middlep + 1) % 4;
    break;
  case '\n':
    switch (*middlep) {
    case 0:
      if (state->selected_account >= 0 &&
          state->selected_account < state->account_count) {
        for (int i = 0; i < state->account_count; i++)
          state->accounts[i].selected = 0;
        state->accounts[state->selected_account].selected = 1;
        account_write(state);
      }
      break;
    case 1:
      new_account_popup(state);
      break;
    case 2:
      delete_account_popup(state, state->selected_account);
      break;
    case 3:
      if (state->selected_account >= 0 &&
          state->selected_account < state->account_count &&
          strcmp(state->accounts[state->selected_account].type, "offline") == 0) {
        set_skin_popup(&state->accounts[state->selected_account]);
        account_write(state);
      }
      break;
    }
    break;
  }

  // 图标栏
  move(0, 0);
  printw("[V]ersion [C]onfig ");
  attron(A_REVERSE);
  printw("[A]ccount");
  attroff(A_REVERSE);
  mvprintw(0, col - 15, "tap [q] to quit");

  // 空状态
  if (state->account_count == 0) {
    mvprintw(4, 4, "No accounts. Press [n]ew to add one.");
    goto bottom;
  }

  // 账户列表
  mvprintw(2, 2, " Type       Name");
  mvprintw(3, 2, " ----       ----");
  int start_y = 4;
  int max_display = row - start_y - 3;

  for (int i = state->scroll_offset;
       i < state->account_count && i < state->scroll_offset + max_display;
       i++) {
    move(start_y + i - state->scroll_offset, 1);
    if (i == state->selected_account) attron(A_REVERSE);

    char star[4];
    strcpy(star, state->accounts[i].selected ? "*" : " ");
    printw("%s %-10s %s", star, state->accounts[i].type,
           state->accounts[i].username);

    if (i == state->selected_account) attroff(A_REVERSE);
  }

  if (state->scroll_offset > 0)
    mvprintw(start_y - 1, 2, "...more above...");
  if (state->scroll_offset + max_display < state->account_count)
    mvprintw(start_y + max_display, 2, "...more below...");

bottom:
  // 底部操作栏
  switch (*middlep) {
  case 0:
    attron(A_REVERSE); mvprintw(row - 1, 0, "[c]hoose"); attroff(A_REVERSE);
    printw(" [n]ew [d]elete [s]kin");
    break;
  case 1:
    mvprintw(row - 1, 0, "[c]hoose ");
    attron(A_REVERSE); printw("[n]ew"); attroff(A_REVERSE);
    printw(" [d]elete [s]kin");
    break;
  case 2:
    mvprintw(row - 1, 0, "[c]hoose [n]ew ");
    attron(A_REVERSE); printw("[d]elete"); attroff(A_REVERSE);
    printw(" [s]kin");
    break;
  case 3:
    mvprintw(row - 1, 0, "[c]hoose [n]ew [d]elete ");
    attron(A_REVERSE); printw("[s]kin"); attroff(A_REVERSE);
    break;
  }
}
