// install.c — 安装新游戏版本
#include "install.h"
#include <cjson/cJSON.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define MANIFEST_URL "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#define MAX_VERSIONS 2000
#define MAX_RETRIES 2

static volatile sig_atomic_t g_cancel = 0;
static void on_sigint(int sig) { g_cancel = 1; }

static void cleanup_install(InstallState *s);
static int dl(const char *url, const char *dest, const char *label);
static int pick_type(int cur);
static int fetch_versions(InstallState *s, const char *type_filter);
static int pick_version(InstallState *s);
static void input_name(char *buf, size_t size);
static void do_install(ConfigState *cstate, const char *version_id,
                       const char *version_url, const char *custom_name,
                       const char *modloader, const char *type);

// ======================== 安装页面主函数 ========================
void install_page(VersionState *vstate, ConfigState *cstate) {
  InstallState s;
  memset(&s, 0, sizeof(s));
  s.selected_row = 1;           // 默认光标在 version 行
  s.type_sel = 0;               // 0=release, 1=snapshot, 2=old_beta, 3=old_alpha

  const char *types[] = {"release", "snapshot", "old_beta", "old_alpha"};
  char user_name[64] = {0};
  char modloader[32] = "vanilla";
  char selected_version[64] = {0};
  char selected_url[512] = {0};

  // 预拉取 manifest
  fetch_versions(&s, types[s.type_sel]);

  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);

    // 顶部
    mvprintw(1, 2, "tap [q] to quit");
    mvprintw(1, col - 25, "Install a new version.");

    // 菜单项
    const char *labels[] = {"Type", "Version", "Name", "Mod Loader"};
    char display_ver[64] = "-- select --";
    if (selected_version[0]) snprintf(display_ver, sizeof(display_ver), "%s", selected_version);
    char display_name[64] = "-- enter name --";
    if (user_name[0]) snprintf(display_name, sizeof(display_name), "%s", user_name);
    char display_type[32];
    snprintf(display_type, sizeof(display_type), "%s", types[s.type_sel]);

    char *values[] = {display_type, display_ver, display_name, modloader};

    int start_y = 4;
    for (int i = 0; i < 4; i++) {
      if (i == s.selected_row) attron(A_REVERSE);
      mvprintw(start_y + i * 2, 4, "  %-12s: %s", labels[i], values[i]);
      if (i == s.selected_row) attroff(A_REVERSE);
    }
    mvprintw(start_y + 8, 4, "  %-12s  %s", "", "");
    if (s.selected_row == 4) attron(A_REVERSE);
    mvprintw(start_y + 8, 6, "[ start installation ]");
    if (s.selected_row == 4) attroff(A_REVERSE);

    int ch = getch();
    switch (ch) {
    case 'q': goto quit;
    case 'j':
      if (s.selected_row < 4) s.selected_row++;
      break;
    case 'k':
      if (s.selected_row > 0) s.selected_row--;
      break;
    case '\n':
      switch (s.selected_row) {
      case 0: // type
        s.type_sel = pick_type(s.type_sel);
        if (s.versions_count > 0) {
          cleanup_install(&s);
        }
        selected_version[0] = '\0';
        selected_url[0] = '\0';
        fetch_versions(&s, types[s.type_sel]);
        s.version_sel = 0;
        s.version_scroll = 0;
        break;
      case 1: // version
        if (s.versions_count == 0) {
          fetch_versions(&s, types[s.type_sel]);
        }
        if (s.versions_count > 0) {
          int picked = pick_version(&s);
          if (picked >= 0) {
            snprintf(selected_version, sizeof(selected_version), "%s",
                     s.version_list[picked]);
            snprintf(selected_url, sizeof(selected_url), "%s",
                     s.version_urls[picked]);
            // 自动生成 name
            if (user_name[0] == '\0') {
              int n = snprintf(user_name, sizeof(user_name), "tmcl_%.54s",
                               selected_version);
              if (n < 0) user_name[0] = '\0';
            }
          }
        }
        break;
      case 2: // name
        input_name(user_name, sizeof(user_name));
        break;
      case 4: // start installation
        if (selected_version[0] && selected_url[0]) {
          do_install(cstate, selected_version, selected_url, user_name,
                     modloader, types[s.type_sel]);
          goto quit;
        }
        break;
      }
      break;
    case 'h':
      if (s.selected_row < 4) s.selected_row = 4; // jump to install
      break;
    }
  }

quit:
  cleanup_install(&s);
  clear();
}

// ======================== 清理 ========================
static void cleanup_install(InstallState *s) {
  if (s->version_list) {
    for (int i = 0; i < s->versions_count; i++)
      free(s->version_list[i]);
    free(s->version_list);
    s->version_list = NULL;
  }
  if (s->version_urls) {
    for (int i = 0; i < s->versions_count; i++)
      free(s->version_urls[i]);
    free(s->version_urls);
    s->version_urls = NULL;
  }
  s->versions_count = 0;
}

// ======================== Type 选择器 ========================
int pick_type(int cur) {
  const char *opts[] = {"release", "snapshot", "old_beta", "old_alpha"};
  int sel = cur;
  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select version type:");
    for (int i = 0; i < 4; i++) {
      if (i == sel) attron(A_REVERSE);
      mvprintw(4 + i, 6, "%s", opts[i]);
      if (i == sel) attroff(A_REVERSE);
    }
    mvprintw(10, 4, "j/k: move  Enter: confirm  q: cancel");
    int c = getch();
    if (c == 'j' && sel < 3) sel++;
    if (c == 'k' && sel > 0) sel--;
    if (c == '\n') break;
    if (c == 'q') { sel = cur; break; }
  }
  clear();
  return sel;
}

// ======================== 从云端拉取版本列表 ========================
static int fetch_versions(InstallState *s, const char *type_filter) {
  cleanup_install(s);

  const char *tmp = "/tmp/tmcl_manifest.json";
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "curl -s -L -o \"%s\" \"" MANIFEST_URL "\"", tmp);
  if (system(cmd) != 0) return 0;

  FILE *f = fopen(tmp, "r");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(sz + 1);
  if (!data) { fclose(f); return 0; }
  fread(data, 1, sz, f);
  data[sz] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(data);
  free(data);
  if (!root) return 0;

  cJSON *versions = cJSON_GetObjectItem(root, "versions");
  if (!versions || !cJSON_IsArray(versions)) {
    cJSON_Delete(root);
    return 0;
  }

  // 两遍扫描：先数匹配的，再分配
  int count = 0;
  cJSON *v;
  cJSON_ArrayForEach(v, versions) {
    cJSON *t = cJSON_GetObjectItem(v, "type");
    if (t && cJSON_IsString(t) && strcmp(t->valuestring, type_filter) == 0)
      count++;
    if (count >= MAX_VERSIONS) break;
  }

  if (count == 0) {
    cJSON_Delete(root);
    return 0;
  }

  s->version_list = malloc(count * sizeof(char *));
  s->version_urls = malloc(count * sizeof(char *));
  if (!s->version_list || !s->version_urls) {
    cJSON_Delete(root);
    return 0;
  }

  int idx = 0;
  cJSON_ArrayForEach(v, versions) {
    cJSON *t = cJSON_GetObjectItem(v, "type");
    if (!t || !cJSON_IsString(t) || strcmp(t->valuestring, type_filter) != 0)
      continue;
    cJSON *id = cJSON_GetObjectItem(v, "id");
    cJSON *url = cJSON_GetObjectItem(v, "url");
    if (id && cJSON_IsString(id) && url && cJSON_IsString(url)) {
      s->version_list[idx] = strdup(id->valuestring);
      s->version_urls[idx] = strdup(url->valuestring);
      idx++;
    }
    if (idx >= count) break;
  }
  s->versions_count = count;
  cJSON_Delete(root);
  return count;
}

// ======================== Version 选择器（带滚动） ========================
static int pick_version(InstallState *s) {
  int sel = s->version_sel;
  int scroll = s->version_scroll;
  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select version (%d available):", s->versions_count);
    int max_disp = row - 6;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + max_disp) scroll = sel - max_disp + 1;
    if (scroll < 0) scroll = 0;

    for (int i = scroll; i < s->versions_count && i < scroll + max_disp; i++) {
      if (i == sel) attron(A_REVERSE);
      mvprintw(4 + i - scroll, 6, "%s", s->version_list[i]);
      if (i == sel) attroff(A_REVERSE);
    }
    if (scroll > 0) mvprintw(3, 6, "... more above ...");
    if (scroll + max_disp < s->versions_count)
      mvprintw(4 + max_disp, 6, "... more below ...");
    mvprintw(row - 1, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' && sel < s->versions_count - 1) sel++;
    if (c == 'k' && sel > 0) sel--;
    if (c == '\n') break;
    if (c == 'q') { sel = -1; break; }
  }
  clear();
  s->version_sel = (sel >= 0) ? sel : 0;
  s->version_scroll = scroll;
  return sel;
}

// ======================== Name 输入 ========================
static void input_name(char *buf, size_t size) {
  echo();
  curs_set(1);
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);
  mvprintw(3, 4, "Enter version name:");
  mvprintw(5, 4, "(letters, digits, '-', '_', '.' only)");
  mvprintw(7, 4, "Name: ");

  char tmp[64] = {0};
  getnstr(tmp, sizeof(tmp) - 1);

  // 过滤非法字符
  char clean[64] = {0};
  int j = 0;
  for (int i = 0; tmp[i] && j < (int)sizeof(clean) - 1; i++) {
    char c = tmp[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      clean[j++] = c;
    }
  }
  clean[j] = '\0';
  if (clean[0] != '\0') {
    strncpy(buf, clean, size - 1);
    buf[size - 1] = '\0';
  }

  noecho();
  curs_set(0);
  clear();
}

// ======================== 下载辅助 ========================

typedef struct {
  char url[1024];
  char dest[512];
  char label[300];
  int result;
} DlTask;

static DlTask *g_tasks = NULL;
static int g_task_count = 0;
static int g_task_idx = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static int dl_one(const char *url, const char *dest, const char *label) {
  // 已取消 → 不启动新下载
  if (g_cancel) {
    printf("  \033[33mSKIP\033[0m %s\n", label);
    return 1;
  }

  // 创建父目录
  char dircmd[2100];
  snprintf(dircmd, sizeof(dircmd), "mkdir -p \"$(dirname '%s')\"", dest);
  system(dircmd);

  pid_t pid = fork();
  if (pid < 0) {
    printf("  \033[31mFORK\033[0m %s\n", label);
    return 1;
  }
  if (pid == 0) {
    execlp("curl", "curl", "-s", "-L", "--connect-timeout", "15",
           "--max-time", "120", "--fail", "--show-error", "-o", dest, url,
           (char *)NULL);
    _exit(1);
  }

  // 父进程：轮询 waitpid，每 100ms 检查 g_cancel
  int status, done = 0;
  while (!done) {
    if (g_cancel) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      printf("  \033[33mSKIP\033[0m %s\n", label);
      return 1;
    }
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (r > 0) { done = 1; break; }
    usleep(100000); // 100ms
  }

  if (!done) { printf("  \033[31mWAIT\033[0m %s\n", label); return 1; }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("  \033[32mOK\033[0m  %s\n", label);
    return 0;
  } else {
    printf("  \033[31mFAIL\033[0m %s\n", label);
    return 1;
  }
}

static void *dl_worker(void *arg) {
  (void)arg;
  while (!g_cancel) {
    pthread_mutex_lock(&g_mutex);
    if (g_task_idx >= g_task_count) {
      pthread_mutex_unlock(&g_mutex);
      break;
    }
    int i = g_task_idx++;
    pthread_mutex_unlock(&g_mutex);

    g_tasks[i].result =
        dl_one(g_tasks[i].url, g_tasks[i].dest, g_tasks[i].label);
  }
  return NULL;
}

static void dl_parallel(DlTask *tasks, int count, int num_threads) {
  if (count <= 0 || num_threads < 1) return;
  g_tasks = tasks;
  g_task_count = count;
  g_task_idx = 0;

  int nth = num_threads;
  if (nth > count) nth = count;
  pthread_t *threads = malloc(nth * sizeof(pthread_t));
  for (int i = 0; i < nth; i++)
    pthread_create(&threads[i], NULL, dl_worker, NULL);
  for (int i = 0; i < nth; i++)
    pthread_join(threads[i], NULL);
  free(threads);
}

// ---- library rules 检查（支持多条 rules，正确处理 allow/disallow）----
static int lib_rules_ok(cJSON *rules) {
  if (!rules || !cJSON_IsArray(rules)) return 1; // 无规则 → 全平台
  int allowed = 0; // 默认不允许
  cJSON *rule;
  cJSON_ArrayForEach(rule, rules) {
    cJSON *action = cJSON_GetObjectItem(rule, "action");
    cJSON *os = cJSON_GetObjectItem(rule, "os");
    cJSON *features = cJSON_GetObjectItem(rule, "features");

    if (!os && !features) {
      // 全局规则
      if (action && strcmp(action->valuestring, "allow") == 0) allowed = 1;
      if (action && strcmp(action->valuestring, "disallow") == 0) allowed = 0;
    } else if (os && !features) {
      cJSON *os_name = cJSON_GetObjectItem(os, "name");
      if (os_name && cJSON_IsString(os_name) &&
          strcmp(os_name->valuestring, "linux") == 0) {
        if (action && strcmp(action->valuestring, "allow") == 0) allowed = 1;
        if (action && strcmp(action->valuestring, "disallow") == 0) allowed = 0;
      }
    }
    // features 规则 → 忽略（TMCL 不支持自定义分辨率/demo 等特性）
  }
  return allowed;
}

// ---- 构建 library 的下载 URL 和路径 ----
static int build_lib_task(DlTask *t, cJSON *lib, const char *mcdir) {
  memset(t, 0, sizeof(*t));

  // 有 downloads.artifact → 直接用其 path 和 url
  cJSON *dl_info = cJSON_GetObjectItem(lib, "downloads");
  if (dl_info) {
    cJSON *artifact = cJSON_GetObjectItem(dl_info, "artifact");
    if (artifact) {
      cJSON *path = cJSON_GetObjectItem(artifact, "path");
      cJSON *url = cJSON_GetObjectItem(artifact, "url");
      if (path && cJSON_IsString(path) && url && cJSON_IsString(url)) {
        snprintf(t->dest, sizeof(t->dest), "%s/libraries/%s", mcdir,
                 path->valuestring);
        snprintf(t->url, sizeof(t->url), "%s", url->valuestring);
        snprintf(t->label, sizeof(t->label), "%s", path->valuestring);
        return 1;
      }
    }
    return 0; // 有 downloads 但无 artifact（如仅含 classifiers 的原生库）
  }

  // 无 downloads → 从 name 构建 maven 路径
  cJSON *name = cJSON_GetObjectItem(lib, "name");
  if (!name || !cJSON_IsString(name)) return 0;

  char *ns = strdup(name->valuestring);
  char *sp;
  char *group = strtok_r(ns, ":", &sp);
  char *artifact = strtok_r(NULL, ":", &sp);
  char *version = strtok_r(NULL, ":", &sp);
  if (!group || !artifact || !version) { free(ns); return 0; }

  char gp[256];
  snprintf(gp, sizeof(gp), "%s", group);
  for (char *p = gp; *p; p++)
    if (*p == '.') *p = '/';

  snprintf(t->url, sizeof(t->url),
           "https://libraries.minecraft.net/%s/%s/%s/%s-%s.jar", gp, artifact,
           version, artifact, version);
  snprintf(t->dest, sizeof(t->dest), "%s/libraries/%s/%s/%s/%s-%s.jar", mcdir,
           gp, artifact, version, artifact, version);
  snprintf(t->label, sizeof(t->label), "%s:%s:%s", group, artifact, version);
  free(ns);
  return 1;
}

// ======================== 执行安装 ========================
// ---- 收集失败任务并进行重试 ----
static int retry_failed(DlTask *tasks, int count, int num_threads,
                        char ***failed_names, int *failed_count) {
  if (g_cancel) return 0;
  // 第一遍：收集失败的任务
  DlTask *retry = malloc(count * sizeof(DlTask));
  int rcount = 0;
  for (int i = 0; i < count; i++) {
    if (tasks[i].result != 0) {
      memcpy(&retry[rcount], &tasks[i], sizeof(DlTask));
      rcount++;
    }
  }
  if (rcount == 0) { free(retry); return 0; }
  printf("\n  Retrying %d failed file(s)...\n\n", rcount);
  if (g_cancel) { free(retry); return 0; }
  dl_parallel(retry, rcount, num_threads);

  // 回写结果
  int ri = 0, still_failed = 0;
  for (int i = 0; i < count; i++) {
    if (tasks[i].result != 0 && ri < rcount) {
      tasks[i].result = retry[ri].result;
      ri++;
    }
    if (tasks[i].result != 0) still_failed++;
  }

  // 收集最终失败的名字
  if (still_failed > 0) {
    *failed_names = malloc(still_failed * sizeof(char *));
    *failed_count = 0;
    for (int i = 0; i < count; i++) {
      if (tasks[i].result != 0) {
        (*failed_names)[(*failed_count)++] = strdup(tasks[i].label);
      }
    }
  }
  free(retry);
  return still_failed;
}

static void do_install(ConfigState *cstate, const char *version_id,
                       const char *version_url, const char *custom_name,
                       const char *modloader, const char *type) {
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1) num_threads = 96;
  char *mcdir = cstate->items[2].value;

  printf("\n=== Installing %s (%s) ===\n", custom_name, version_id);
  printf("    Threads: 8 (libs) / %d (assets)\n\n", num_threads);

  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  // 1) 下载版本 JSON
  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);
  if (dl_one(version_url, json_path, "version JSON") != 0) {
    printf("\nFailed to download version JSON. Aborting.\n");
    goto done;
  }

  // 2) 解析版本 JSON
  FILE *jf = fopen(json_path, "r");
  if (!jf) { printf("Failed to read version JSON.\n"); goto done; }
  fseek(jf, 0, SEEK_END);
  long jsz = ftell(jf);
  fseek(jf, 0, SEEK_SET);
  char *jdata = malloc(jsz + 1);
  fread(jdata, 1, jsz, jf);
  jdata[jsz] = '\0';
  fclose(jf);

  cJSON *root = cJSON_Parse(jdata);
  free(jdata);
  if (!root) { printf("Failed to parse version JSON.\n"); goto done; }

  // ---- Phase 1: 收集所有下载任务 ----
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  int tcount = 0;

  // a) client.jar
  cJSON *downloads = cJSON_GetObjectItem(root, "downloads");
  if (downloads) {
    cJSON *client = cJSON_GetObjectItem(downloads, "client");
    if (client) {
      cJSON *jar_url = cJSON_GetObjectItem(client, "url");
      if (jar_url && cJSON_IsString(jar_url)) {
        snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                 jar_url->valuestring);
        snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s/%s.jar",
                 version_dir, custom_name);
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label), "client.jar");
        tcount++;
      }
    }
  }

  // b) libraries
  cJSON *libraries = cJSON_GetObjectItem(root, "libraries");
  int lib_match = 0;
  if (libraries && cJSON_IsArray(libraries)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, libraries) {
      cJSON *rules = cJSON_GetObjectItem(lib, "rules");
      if (lib_rules_ok(rules)) lib_match++;
    }

    int cur = 0;
    cJSON_ArrayForEach(lib, libraries) {
      cJSON *rules = cJSON_GetObjectItem(lib, "rules");
      if (!lib_rules_ok(rules)) continue;
      cur++;
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[%d/%d] %s", cur, lib_match, t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // c) asset index
  char ai_url[1024] = {0};
  char ai_path[512] = {0};
  cJSON *assetIndex = cJSON_GetObjectItem(root, "assetIndex");
  if (assetIndex) {
    cJSON *ai_u = cJSON_GetObjectItem(assetIndex, "url");
    cJSON *ai_id = cJSON_GetObjectItem(assetIndex, "id");
    if (ai_u && cJSON_IsString(ai_u) && ai_id && cJSON_IsString(ai_id)) {
      snprintf(ai_url, sizeof(ai_url), "%s", ai_u->valuestring);
      snprintf(ai_path, sizeof(ai_path), "%s/assets/indexes/%s.json", mcdir,
               ai_id->valuestring);
      snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s", ai_url);
      snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s", ai_path);
      snprintf(tasks[tcount].label, sizeof(tasks[tcount].label), "asset index");
      tcount++;
    }
  }

  // ---- Phase 2: 并行下载 ----
  printf("  Downloading %d files...\n\n", tcount);
  dl_parallel(tasks, tcount, 8);

  // 重试失败的文件（最多 MAX_RETRIES 次）
  cJSON_Delete(root);
  root = NULL;
  char **failed_names = NULL;
  int failed_count = 0;
  if (g_cancel) {
    printf("\n  Cancelled by user, skipping retries.\n");
    goto cancelled;
  }
  for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
    int f = retry_failed(tasks, tcount, 8, &failed_names,
                         &failed_count);
    if (f == 0) break;
  }

  // ---- Phase 3: 下载 asset 文件 ----
  if (ai_path[0] && !g_cancel) {
    FILE *aif = fopen(ai_path, "r");
    if (aif) {
      fseek(aif, 0, SEEK_END);
      long asz = ftell(aif);
      fseek(aif, 0, SEEK_SET);
      char *ai_data = malloc(asz + 1);
      if (ai_data) {
        fread(ai_data, 1, asz, aif);
        ai_data[asz] = '\0';
      }
      fclose(aif);

      cJSON *ai_root = NULL;
      if (ai_data) ai_root = cJSON_Parse(ai_data);
      if (ai_root) {
        cJSON *objects = cJSON_GetObjectItem(ai_root, "objects");
        if (objects && cJSON_IsObject(objects)) {
          // 第一遍：统计需要下载的 asset 数量
          int need_dl = 0, already = 0;
          cJSON *obj_item;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item)) continue;
            const char *hash = hash_item->valuestring;
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%c%c/%s", mcdir,
                     hash[0], hash[1], hash);
            if (access(dest, F_OK) == 0) already++;
            else need_dl++;
          }

          DlTask *atasks = malloc((need_dl + 1) * sizeof(DlTask));
          int ac = 0;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item)) continue;
            const char *hash = hash_item->valuestring;
            char first2[3] = {hash[0], hash[1], '\0'};
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%s/%s", mcdir,
                     first2, hash);
            if (access(dest, F_OK) == 0) continue;

            snprintf(atasks[ac].url, sizeof(atasks[ac].url),
                     "https://resources.download.minecraft.net/%s/%s", first2,
                     hash);
            snprintf(atasks[ac].dest, sizeof(atasks[ac].dest), "%s", dest);
            snprintf(atasks[ac].label, sizeof(atasks[ac].label),
                     "[%d/%d] %s", ac + 1, need_dl, obj_item->string);
            ac++;
          }
          if (ac > 0) {
            printf("\n  Assets: %d need download (%d already cached)\n\n",
                   ac, already);
            dl_parallel(atasks, ac, num_threads);
            // 对 asset 也做重试
            char **a_fail = NULL;
            int a_fc = 0;
            for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
              int f = retry_failed(atasks, ac, num_threads, &a_fail, &a_fc);
              if (f == 0) break;
              // 合并失败列表
              if (a_fc > 0) {
                failed_names = realloc(failed_names, (failed_count + a_fc) * sizeof(char *));
                for (int i = 0; i < a_fc; i++)
                  failed_names[failed_count++] = a_fail[i];
                free(a_fail);
              }
            }
          } else {
            printf("\n  Assets: all %d already cached\n", already);
          }
          free(atasks);
        }
        cJSON_Delete(ai_root);
      }
      free(ai_data);
    }
  }

  free(tasks);

cancelled:
  // ---- 打印失败总结 ----
  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
    printf("\nYou may run the installer again to retry missing files.\n");
  }

  printf("\n=== Installation complete: %s ===\n", custom_name);
  if (g_cancel) printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {}

done:
  if (root) cJSON_Delete(root);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}
