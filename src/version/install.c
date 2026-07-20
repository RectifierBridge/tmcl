#define _GNU_SOURCE
// install.c — 安装新游戏版本
#include "install.h"
#include <cjson/cJSON.h>
#include <errno.h>
#include <ncurses.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MANIFEST_URL                                                           \
  "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
#define MAX_VERSIONS 2000
#define MAX_RETRIES 2

static const ForgeConfig FORGE_CFG = {
    .maven_base = "maven.minecraftforge.net",
    .group_id = "net.minecraftforge",
    .artifact_id = "forge",
    .has_promotions = 1,
};

static const ForgeConfig NEOFORGE_CFG = {
    .maven_base = "maven.neoforged.net/releases",
    .group_id = "net.neoforged",
    .artifact_id = "neoforge",
    .has_promotions = 0,
};

static volatile sig_atomic_t g_cancel = 0;
static void on_sigint(int sig) { g_cancel = 1; }

// Forge processor 执行期间，installer jar 和 MC 版本信息
static char g_installer_path[1024];
static char g_mc_version[64];

static void cleanup_install(InstallState *s);
static int pick_type(int cur);
static int fetch_versions(InstallState *s, const char *type_filter);
static int pick_version(InstallState *s);
static void input_name(char *buf, size_t size);
static void do_install(ConfigState *cstate, const char *version_id,
                       const char *version_url, const char *custom_name,
                       const char *modloader, const char *type,
                       const char *fabric_loader_ver);
static char **fabric_loader_versions(const char *mc_version, int *count_out);
static char *pick_fabric_loader(char **versions, int count);
static char *pick_forge_version(char **versions, int count,
                                const ForgeConfig *fcfg);
static char **parse_maven_xml(const char *xml_data, const char *mc_version,
                              int *count_out, int neoforge_mode,
                              const char *mc_type);
static int get_jar_main_class(const char *jar_path, char *out, size_t out_size);

// 判断 MC 版本是否为旧版 Forge（minor ≤ 12）
static int is_legacy_forge(const char *mc_version) {
  if (!mc_version)
    return 0;
  const char *dot1 = strchr(mc_version, '.');
  if (!dot1)
    return 0;
  const char *dot2 = strchr(dot1 + 1, '.');
  size_t len = dot2 ? (size_t)(dot2 - dot1 - 1) : strlen(dot1 + 1);
  if (len == 0 || len > 4)
    return 0;
  char minor[8];
  memcpy(minor, dot1 + 1, len);
  minor[len] = '\0';
  return atoi(minor) <= 12;
}

// ======================== Mod Loader 选择器 ========================
static int pick_modloader(int cur) {
  const char *opts[] = {"vanilla", "fabric", "forge", "neoforge"};
  int sel = cur, row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select mod loader:");
    for (int i = 0; i < 4; i++) {
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(4 + i, 6, "%s", opts[i]);
      if (i == sel)
        attroff(A_REVERSE);
    }
    mvprintw(10, 4, "j/k: move  Enter: confirm  q: cancel");
    int c = getch();
    if (c == 'j' || c == KEY_DOWN && sel < 3)
      sel++;
    if (c == 'k' || c == KEY_UP && sel > 0)
      sel--;
    if (c == '\n')
      break;
    if (c == 'q') {
      sel = cur;
      break;
    }
  }
  clear();
  return sel;
}

// ======================== 安装页面主函数 ========================
void install_page(VersionState *vstate, ConfigState *cstate) {
  InstallState s;
  memset(&s, 0, sizeof(s));
  s.selected_row = 1; // 默认光标在 version 行
  s.type_sel = 0;     // 0=release, 1=snapshot, 2=old_beta, 3=old_alpha

  const char *types[] = {"release", "snapshot", "old_beta", "old_alpha"};
  const char *modloaders[] = {"vanilla", "fabric", "forge", "neoforge"};
  char user_name[64] = {0};
  char modloader[32] = "vanilla";
  int modloader_sel = 0;
  char selected_version[64] = {0};
  char selected_url[512] = {0};
  char *fabric_loader_ver = NULL; // 用户选择的 Fabric Loader 版本
  char *forge_ver = NULL;         // 用户选择的 Forge/NeoForge 版本

  // 预拉取 manifest
  fetch_versions(&s, types[s.type_sel]);

  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);

    // 顶部
    mvprintw(1, 2, "tap [q] to quit");
    mvprintw(1, col - 25, "Install a new version.");

    // 菜单项：Type, Version, Mod Loader, Name
    const char *labels[] = {"Type", "Version", "Mod Loader", "Name"};
    char display_ver[64] = "-- select --";
    if (selected_version[0])
      snprintf(display_ver, sizeof(display_ver), "%s", selected_version);
    char display_name[64] = "-- enter name --";
    if (user_name[0])
      snprintf(display_name, sizeof(display_name), "%s", user_name);
    char display_type[32];
    snprintf(display_type, sizeof(display_type), "%s", types[s.type_sel]);

    char *values[] = {display_type, display_ver, modloader, display_name};

    int start_y = 4;
    for (int i = 0; i < 4; i++) {
      if (i == s.selected_row)
        attron(A_REVERSE);
      mvprintw(start_y + i * 2, 4, "  %-12s: %s", labels[i], values[i]);
      if (i == s.selected_row)
        attroff(A_REVERSE);
    }
    mvprintw(start_y + 8, 4, "  %-12s  %s", "", "");
    if (s.selected_row == 4)
      attron(A_REVERSE);
    mvprintw(start_y + 8, 6, "[ start installation ]");
    if (s.selected_row == 4)
      attroff(A_REVERSE);

    int ch = getch();
    switch (ch) {
    case 'q':
      goto quit;
    case 'j':
    case KEY_DOWN:
      if (s.selected_row < 4)
        s.selected_row++;
      break;
    case 'k':
    case KEY_UP:
      if (s.selected_row > 0)
        s.selected_row--;
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
        free(fabric_loader_ver);
        fabric_loader_ver = NULL;
        free(forge_ver);
        forge_ver = NULL;
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
            // MC 版本变化 → 之前选择的 forge 版本作废
            free(forge_ver);
            forge_ver = NULL;
            // 自动生成 name（仅当用户未手动修改时）
            if (user_name[0] == '\0') {
              if (modloader_sel == 0)
                snprintf(user_name, sizeof(user_name), "tmcl_%.54s",
                         selected_version);
              else
                snprintf(user_name, sizeof(user_name), "tmcl_%s-%.48s",
                         modloaders[modloader_sel], selected_version);
            }
          }
        }
        break;
      case 2: // mod loader
        modloader_sel = pick_modloader(modloader_sel);
        strncpy(modloader, modloaders[modloader_sel], sizeof(modloader) - 1);
        // Fabric：选择 loader 版本
        free(fabric_loader_ver);
        fabric_loader_ver = NULL;
        if (modloader_sel == 1 && selected_version[0]) {
          int lcount = 0;
          char **lvers = fabric_loader_versions(selected_version, &lcount);
          if (lvers && lcount > 0) {
            fabric_loader_ver = pick_fabric_loader(lvers, lcount);
            for (int i = 0; i < lcount; i++)
              free(lvers[i]);
            free(lvers);
          }
          if (!fabric_loader_ver) {
            // 获取失败或用户取消 → 回退到 vanilla
            modloader_sel = 0;
            strncpy(modloader, "vanilla", sizeof(modloader) - 1);
          }
        }
        // Forge / NeoForge：选择版本
        free(forge_ver);
        forge_ver = NULL;
        if (modloader_sel >= 2 && selected_version[0]) {
          const ForgeConfig *pick_fcfg =
              (modloader_sel == 2) ? &FORGE_CFG : &NEOFORGE_CFG;
          // 构建 Maven URL（group_id 中 . → /）
          char gpath[256];
          snprintf(gpath, sizeof(gpath), "%s", pick_fcfg->group_id);
          for (char *pp = gpath; *pp; pp++)
            if (*pp == '.')
              *pp = '/';
          char murl[1024];
          snprintf(murl, sizeof(murl), "https://%s/%s/%s/maven-metadata.xml",
                   pick_fcfg->maven_base, gpath, pick_fcfg->artifact_id);
          char fetch_cmd[1536];
          snprintf(fetch_cmd, sizeof(fetch_cmd),
                   "curl -sL -o /tmp/tmcl_forge_meta.xml '%s'", murl);
          if (system(fetch_cmd) == 0) {
            FILE *mf = fopen("/tmp/tmcl_forge_meta.xml", "r");
            if (mf) {
              fseek(mf, 0, SEEK_END);
              long msz = ftell(mf);
              fseek(mf, 0, SEEK_SET);
              char *mdata = malloc(msz + 1);
              if (mdata) {
                fread(mdata, 1, msz, mf);
                mdata[msz] = '\0';
                int fcount = 0;
                char **fvers = parse_maven_xml(
                    mdata, selected_version, &fcount,
                    /*neoforge_mode=*/(modloader_sel == 3), types[s.type_sel]);
                free(mdata);
                if (fvers && fcount > 0) {
                  forge_ver = pick_forge_version(fvers, fcount, pick_fcfg);
                  for (int i = 0; i < fcount; i++)
                    free(fvers[i]);
                  free(fvers);
                }
              }
              fclose(mf);
            }
            remove("/tmp/tmcl_forge_meta.xml");
          }
          if (!forge_ver) {
            // 获取失败或用户取消 → 回退到 vanilla
            modloader_sel = 0;
            strncpy(modloader, "vanilla", sizeof(modloader) - 1);
          }
        }
        break;
      case 3: // name
        input_name(user_name, sizeof(user_name));
        break;
      case 4: // start installation
        if (selected_version[0] && selected_url[0]) {
          if (modloader_sel >= 2) { // forge / neoforge
            const ForgeConfig *fcfg =
                (modloader_sel == 2) ? &FORGE_CFG : &NEOFORGE_CFG;
            if (modloader_sel == 2 && is_legacy_forge(selected_version)) {
              do_legacy_forge_install(cstate, selected_version, selected_url,
                                      user_name, modloaders[modloader_sel],
                                      types[s.type_sel], fcfg, forge_ver);
            } else {
              do_forge_install(cstate, selected_version, selected_url,
                               user_name, modloaders[modloader_sel],
                               types[s.type_sel], fcfg, forge_ver);
            }
            goto quit;
          } else {
            do_install(cstate, selected_version, selected_url, user_name,
                       modloader, types[s.type_sel], fabric_loader_ver);
            goto quit;
          }
        }
        break;
      }
      s.selected_row++;
      break;
    }
  }

quit:
  free(fabric_loader_ver);
  free(forge_ver);
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
static int pick_type(int cur) {
  const char *opts[] = {"release", "snapshot", "old_beta", "old_alpha"};
  int sel = cur;
  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select version type:");
    for (int i = 0; i < 4; i++) {
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(4 + i, 6, "%s", opts[i]);
      if (i == sel)
        attroff(A_REVERSE);
    }
    mvprintw(10, 4, "j/k: move  Enter: confirm  q: cancel");
    int c = getch();
    if (c == 'j' || c == KEY_DOWN && sel < 3)
      sel++;
    if (c == 'k' || c == KEY_UP && sel > 0)
      sel--;
    if (c == '\n')
      break;
    if (c == 'q') {
      sel = cur;
      break;
    }
  }
  clear();
  return sel;
}

// ======================== 从云端拉取版本列表 ========================
static int fetch_versions(InstallState *s, const char *type_filter) {
  cleanup_install(s);

  const char *tmp = "/tmp/tmcl_manifest.json";
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "curl -s -L -o \"%s\" \"" MANIFEST_URL "\"", tmp);
  if (system(cmd) != 0)
    return 0;

  FILE *f = fopen(tmp, "r");
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(sz + 1);
  if (!data) {
    fclose(f);
    return 0;
  }
  fread(data, 1, sz, f);
  data[sz] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(data);
  free(data);
  if (!root)
    return 0;

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
    if (count >= MAX_VERSIONS)
      break;
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
    if (idx >= count)
      break;
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
    if (sel < scroll)
      scroll = sel;
    if (sel >= scroll + max_disp)
      scroll = sel - max_disp + 1;
    if (scroll < 0)
      scroll = 0;

    for (int i = scroll; i < s->versions_count && i < scroll + max_disp; i++) {
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(4 + i - scroll, 6, "%s", s->version_list[i]);
      if (i == sel)
        attroff(A_REVERSE);
    }
    if (scroll > 0)
      mvprintw(3, 6, "... more above ...");
    if (scroll + max_disp < s->versions_count)
      mvprintw(4 + max_disp, 6, "... more below ...");
    mvprintw(row - 1, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' || c == KEY_DOWN && sel < s->versions_count - 1)
      sel++;
    if (c == 'k' || c == KEY_UP && sel > 0)
      sel--;
    if (c == '\n')
      break;
    if (c == 'q') {
      sel = -1;
      break;
    }
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
    execlp("curl", "curl", "-s", "-L", "--connect-timeout", "15", "--max-time",
           "120", "--fail", "--show-error", "-o", dest, url, (char *)NULL);
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
      if (errno == EINTR)
        continue;
      break;
    }
    if (r > 0) {
      done = 1;
      break;
    }
    usleep(100000); // 100ms
  }

  if (!done) {
    printf("  \033[31mWAIT\033[0m %s\n", label);
    return 1;
  }

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    // 对 .jar 文件验证 zip 完整性
    size_t dlen = strlen(dest);
    if (dlen > 4 && strcmp(dest + dlen - 4, ".jar") == 0) {
      FILE *vf = fopen(dest, "rb");
      int valid = 0;
      if (vf) {
        unsigned char magic[2];
        if (fread(magic, 1, 2, vf) == 2 && magic[0] == 'P' && magic[1] == 'K') {
          fseek(vf, -22, SEEK_END);
          unsigned char end[4];
          if (fread(end, 1, 4, vf) == 4 && end[0] == 0x50 && end[1] == 0x4b &&
              end[2] == 0x05 && end[3] == 0x06)
            valid = 1;
        }
        fclose(vf);
      }
      if (!valid) {
        remove(dest);
        printf("  \033[31mCORRUPT\033[0m %s\n", label);
        return 1;
      }
    }
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
  if (count <= 0 || num_threads < 1)
    return;
  g_tasks = tasks;
  g_task_count = count;
  g_task_idx = 0;

  int nth = num_threads;
  if (nth > count)
    nth = count;
  pthread_t *threads = malloc(nth * sizeof(pthread_t));
  for (int i = 0; i < nth; i++)
    pthread_create(&threads[i], NULL, dl_worker, NULL);
  for (int i = 0; i < nth; i++)
    pthread_join(threads[i], NULL);
  free(threads);
}

// ---- library rules 检查（支持多条 rules，正确处理 allow/disallow）----
static int lib_rules_ok(cJSON *rules) {
  if (!rules || !cJSON_IsArray(rules))
    return 1;      // 无规则 → 全平台
  int allowed = 0; // 默认不允许
  cJSON *rule;
  cJSON_ArrayForEach(rule, rules) {
    cJSON *action = cJSON_GetObjectItem(rule, "action");
    cJSON *os = cJSON_GetObjectItem(rule, "os");
    cJSON *features = cJSON_GetObjectItem(rule, "features");

    if (!os && !features) {
      // 全局规则
      if (action && strcmp(action->valuestring, "allow") == 0)
        allowed = 1;
      if (action && strcmp(action->valuestring, "disallow") == 0)
        allowed = 0;
    } else if (os && !features) {
      cJSON *os_name = cJSON_GetObjectItem(os, "name");
      if (os_name && cJSON_IsString(os_name) &&
          strcmp(os_name->valuestring, "linux") == 0) {
        if (action && strcmp(action->valuestring, "allow") == 0)
          allowed = 1;
        if (action && strcmp(action->valuestring, "disallow") == 0)
          allowed = 0;
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
  if (!name || !cJSON_IsString(name))
    return 0;

  char *ns = strdup(name->valuestring);
  char *sp;
  char *group = strtok_r(ns, ":", &sp);
  char *artifact = strtok_r(NULL, ":", &sp);
  char *version = strtok_r(NULL, ":", &sp);
  char *classifier = strtok_r(NULL, ":", &sp); // 可能为 NULL
  if (!group || !artifact || !version) {
    free(ns);
    return 0;
  }

  // 处理 @ext 后缀（如 classifier@zip, version@jar）
  char ext[16] = "jar";
  {
    char *at_sign = strchr(version, '@');
    if (at_sign) {
      snprintf(ext, sizeof(ext), "%s", at_sign + 1);
      *at_sign = '\0';
    }
    if (classifier) {
      at_sign = strchr(classifier, '@');
      if (at_sign) {
        snprintf(ext, sizeof(ext), "%s", at_sign + 1);
        *at_sign = '\0';
      }
    }
  }

  char gp[256];
  snprintf(gp, sizeof(gp), "%s", group);
  for (char *p = gp; *p; p++)
    if (*p == '.')
      *p = '/';

  // 优先用 library 自带的 url（Fabric/Forge 等在自有 maven 上），
  // 否则回退到 Mojang 官方 maven
  char base_url[512];
  snprintf(base_url, sizeof(base_url), "https://libraries.minecraft.net/");
  cJSON *lib_url = cJSON_GetObjectItem(lib, "url");
  if (lib_url && cJSON_IsString(lib_url) && lib_url->valuestring[0]) {
    snprintf(base_url, sizeof(base_url), "%s", lib_url->valuestring);
    // 强制升级 http:// → https://（Maven Central 等已禁用 HTTP）
    if (strncmp(base_url, "http://", 7) == 0) {
      memmove(base_url + 8, base_url + 7, strlen(base_url + 7) + 1);
      memcpy(base_url, "https://", 8);
    }
  }

  if (classifier) {
    snprintf(t->url, sizeof(t->url), "%s%s/%s/%s/%s-%s-%s.%s", base_url, gp,
             artifact, version, artifact, version, classifier, ext);
    snprintf(t->dest, sizeof(t->dest), "%s/libraries/%s/%s/%s/%s-%s-%s.%s",
             mcdir, gp, artifact, version, artifact, version, classifier, ext);
    snprintf(t->label, sizeof(t->label), "%s:%s:%s:%s", group, artifact,
             version, classifier);
  } else {
    snprintf(t->url, sizeof(t->url), "%s%s/%s/%s/%s-%s.%s", base_url, gp,
             artifact, version, artifact, version, ext);
    snprintf(t->dest, sizeof(t->dest), "%s/libraries/%s/%s/%s/%s-%s.%s", mcdir,
             gp, artifact, version, artifact, version, ext);
    snprintf(t->label, sizeof(t->label), "%s:%s:%s", group, artifact, version);
  }
  free(ns);
  return 1;
}

// ======================== 执行安装 ========================
// ---- 收集失败任务并进行重试 ----
static int retry_failed(DlTask *tasks, int count, int num_threads,
                        char ***failed_names, int *failed_count) {
  if (g_cancel)
    return 0;
  // 第一遍：收集失败的任务
  DlTask *retry = malloc(count * sizeof(DlTask));
  int rcount = 0;
  for (int i = 0; i < count; i++) {
    if (tasks[i].result != 0) {
      memcpy(&retry[rcount], &tasks[i], sizeof(DlTask));
      rcount++;
    }
  }
  if (rcount == 0) {
    free(retry);
    return 0;
  }
  printf("\n  Retrying %d failed file(s)...\n\n", rcount);
  if (g_cancel) {
    free(retry);
    return 0;
  }
  dl_parallel(retry, rcount, num_threads);

  // 回写结果
  int ri = 0, still_failed = 0;
  for (int i = 0; i < count; i++) {
    if (tasks[i].result != 0 && ri < rcount) {
      tasks[i].result = retry[ri].result;
      ri++;
    }
    if (tasks[i].result != 0)
      still_failed++;
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

// 获取所有可用的 Fabric Loader 版本列表（返回 strdup
// 的字符串数组，调用者负责释放）
static char **fabric_loader_versions(const char *mc_version, int *count_out) {
  *count_out = 0;
  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "curl -sL -o /tmp/tmcl_fabric_loaders.json "
           "'https://meta.fabricmc.net/v2/versions/loader/%s'",
           mc_version);
  if (system(cmd) != 0)
    return NULL;

  FILE *f = fopen("/tmp/tmcl_fabric_loaders.json", "r");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(sz + 1);
  if (!data) {
    fclose(f);
    return NULL;
  }
  fread(data, 1, sz, f);
  data[sz] = '\0';
  fclose(f);

  cJSON *arr = cJSON_Parse(data);
  free(data);
  if (!arr || !cJSON_IsArray(arr)) {
    cJSON_Delete(arr);
    return NULL;
  }

  int total = cJSON_GetArraySize(arr);
  if (total == 0) {
    cJSON_Delete(arr);
    return NULL;
  }

  char **versions = malloc(total * sizeof(char *));
  int idx = 0;
  for (int i = 0; i < total; i++) {
    cJSON *item = cJSON_GetArrayItem(arr, i);
    cJSON *loader = cJSON_GetObjectItem(item, "loader");
    if (!loader)
      continue;
    cJSON *ver = cJSON_GetObjectItem(loader, "version");
    if (!ver || !cJSON_IsString(ver))
      continue;
    versions[idx++] = strdup(ver->valuestring);
  }
  cJSON_Delete(arr);
  *count_out = idx;
  return versions;
}

// Fabric Loader 版本选择器（ncurses UI）
static char *pick_fabric_loader(char **versions, int count) {
  int sel = 0, scroll = 0;
  int row, col;
  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select Fabric Loader version (%d available):", count);
    int max_disp = row - 6;
    if (sel < scroll)
      scroll = sel;
    if (sel >= scroll + max_disp)
      scroll = sel - max_disp + 1;
    if (scroll < 0)
      scroll = 0;

    for (int i = scroll; i < count && i < scroll + max_disp; i++) {
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(4 + i - scroll, 6, "%s", versions[i]);
      if (i == sel)
        attroff(A_REVERSE);
    }
    if (scroll > 0)
      mvprintw(3, 6, "... more above ...");
    if (scroll + max_disp < count)
      mvprintw(4 + max_disp, 6, "... more below ...");
    mvprintw(row - 1, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' || c == KEY_DOWN && sel < count - 1)
      sel++;
    if (c == 'k' || c == KEY_UP && sel > 0)
      sel--;
    if (c == '\n')
      break;
    if (c == 'q') {
      clear();
      return NULL;
    }
  }
  clear();
  return strdup(versions[sel]);
}

// ======================== Maven metadata XML 解析 ========================
// 从 maven-metadata.xml 中提取所有匹配 mc_version 前缀的版本号
// 返回 strdup 的字符串数组，*count_out 为数量，调用者负责释放
// mc_type: Mojang 版本类型 ("release" / "snapshot" / "old_beta" / "old_alpha")
//          用于过滤 NeoForge 版本（release 时排除 alpha/beta/snapshot 版本）
//
// NeoForge 版本号与 MC 版本号的对应关系：
//   MC 1.20.n          →  NeoForge 20.n.x         (去除 "1." 前缀，原样保留)
//   MC 1.21   (无 .0)  →  NeoForge 21.0.x         (仅 1 段 → 补 .0)
//   MC 1.21.n          →  NeoForge 21.n.x         (2 段 → 原样)
//   MC 26.1   (无 .0)  →  NeoForge 26.1.0.x       (2 部分 → 补 .0)
//   MC 26.1.n          →  NeoForge 26.1.n.x       (3+ 部分 → 原样)
//   MC 26.2   (无 .0)  →  NeoForge 26.2.0.x       (2 部分 → 补 .0)
//
// 未来工作：对于 Forge（fcfg->has_promotions == 1），可以额外获取
// https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json
// 来获取 recommended / latest 标记，优先推荐这些版本给用户。
// 当前纯依赖 maven-metadata.xml 的版本列表已足够使用。
static char **parse_maven_xml(const char *xml_data, const char *mc_version,
                              int *count_out, int neoforge_mode,
                              const char *mc_type) {
  *count_out = 0;
  if (!xml_data || !mc_version)
    return NULL;

  // 动态数组：边扫描边收集匹配的版本
  int cap = 0;
  char **result = NULL;
  const char *p = xml_data;
  char prefix[64];
  char suffix_filter[32] = {
      0};                // 快照版精确匹配后缀（如 "+pre-3", "+snapshot-11"）
  int filter_stable = 0; // release 模式下过滤 alpha/snapshot NeoForge 版本
  if (neoforge_mode) {
    // === 解析 MC 版本号，检测快照/预发布后缀 ===
    // MC "26.1-pre-3" → clean="26.1", suffix_filter="+pre-3"
    // MC "26.1-snapshot-11" → clean="26.1", suffix_filter="+snapshot-11"
    // MC "26.1" → clean="26.1", suffix_filter=""（正式版）
    char clean_mc[64];
    snprintf(clean_mc, sizeof(clean_mc), "%s", mc_version);
    const char *suffix_patterns[] = {"-pre", "-snapshot", "-rc"};
    for (int si = 0; si < 3; si++) {
      char *dash = strstr(clean_mc, suffix_patterns[si]);
      if (dash) {
        // 存后缀供精确匹配：将 "-pre-3" 转为 "+pre-3"
        snprintf(suffix_filter, sizeof(suffix_filter), "+%s", dash + 1);
        *dash = '\0';
        break;
      }
    }

    const char *base = clean_mc;
    if (strncmp(base, "1.", 2) == 0)
      base += 2;

    // 统计 clean MC 版本的总点数，判断组成部分数量：
    //   1 个点 = 2 部分（如 1.21, 26.1, 26.2）→ 前缀需补 ".0."
    //   2+ 个点 = 3+ 部分（如 1.21.1, 26.1.2）→ 原样使用
    int total_dots = 0;
    for (const char *cp = clean_mc; *cp; cp++)
      if (*cp == '.')
        total_dots++;

    if (suffix_filter[0]) {
      // 快照/预发布版：使用 alpha 专用前缀，精确匹配 MC 版本号
      // MC "26.1-pre-3" → NeoForge 前缀 "26.1.0.0-alpha." + 后缀过滤 "+pre-3"
      // MC "26.1-snapshot-11" → 前缀 "26.1.0.0-alpha." + 后缀 "+snapshot-11"
      char base_prefix[64];
      if (total_dots == 1)
        snprintf(base_prefix, sizeof(base_prefix), "%s.0.", base);
      else
        snprintf(base_prefix, sizeof(base_prefix), "%s.", base);
      snprintf(prefix, sizeof(prefix), "%s0-alpha.", base_prefix);
    } else {
      // 正式版：普通前缀
      if (total_dots == 1)
        snprintf(prefix, sizeof(prefix), "%s.0.", base);
      else
        snprintf(prefix, sizeof(prefix), "%s.", base);
    }

    // MC 正式版（type == "release"）排除 alpha/snapshot 标记的 NeoForge 版本
    if (mc_type && strcmp(mc_type, "release") == 0)
      filter_stable = 1;
  } else {
    snprintf(prefix, sizeof(prefix), "%s-", mc_version);
  }
  size_t plen = strlen(prefix);

  while (*p) {
    // 找 <version> 标签
    const char *tag = strstr(p, "<version>");
    if (!tag)
      break;
    tag += 9; // 跳过 "<version>"
    const char *end = strstr(tag, "</version>");
    if (!end)
      break;

    size_t vlen = end - tag;
    if (vlen > 0 && vlen < 128 && strncmp(tag, prefix, plen) == 0) {
      // release 模式：过滤掉 MC 快照/预发布版对应的 NeoForge 版本
      // 这些版本号中含有 "alpha" 或 "snapshot" 标记
      // （如 26.1.0.0-alpha.14+snapshot-11 对应 MC 26.1-snapshot），
      // 但保留 -beta 后缀的版本（对应 MC 正式版，仅 NeoForge 自身为 beta 质量）
      // 注意：必须限制搜索范围不超过 </version>（end 指针），
      // 否则 strstr 会跨越当前标签匹配到后续版本的内容
      if (filter_stable) {
        const char *rest = tag + plen;
        const char *alpha_pos = strstr(rest, "alpha");
        const char *snap_pos = strstr(rest, "snapshot");
        if ((alpha_pos && alpha_pos < end) || (snap_pos && snap_pos < end)) {
          p = end + 10;
          continue;
        }
      }

      // 快照/预发布版：精确匹配后缀（如 "+pre-3", "+snapshot-11"）
      // MC "26.1-pre-3" 只应匹配 NeoForge "26.1.0.0-alpha.X+pre-3" 版本
      // 注意：同样必须限制搜索范围不超过 </version>，防止跨标签误匹配
      if (suffix_filter[0]) {
        const char *sfound = strstr(tag, suffix_filter);
        if (!sfound || sfound >= end) {
          p = end + 10;
          continue;
        }
      }

      // 匹配：扩展容量
      if (*count_out >= cap) {
        int new_cap = cap ? cap * 2 : 32;
        char **new_result = realloc(result, new_cap * sizeof(char *));
        if (!new_result) {
          for (int k = 0; k < *count_out; k++)
            free(result[k]);
          free(result);
          *count_out = 0;
          return NULL;
        }
        result = new_result;
        cap = new_cap;
      }
      char *ver = malloc(vlen + 1);
      memcpy(ver, tag, vlen);
      ver[vlen] = '\0';
      result[*count_out] = ver;
      (*count_out)++;
    }
    p = end + 10; // 跳过 "</version>"
  }

  // 反转数组使新版本在前（XML 中旧版本在前）
  if (result && *count_out > 1) {
    // 按版本号降序排列（高版本在前），数值感知比较
    for (int i = 0; i < *count_out - 1; i++) {
      for (int j = 0; j < *count_out - 1 - i; j++) {
        const char *a = result[j];
        const char *b = result[j + 1];
        int cmp = 0;
        while (*a && *b && cmp == 0) {
          if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long na = strtol(a, (char **)&a, 10);
            long nb = strtol(b, (char **)&b, 10);
            if (na < nb)
              cmp = -1;
            else if (na > nb)
              cmp = 1;
          } else {
            if (*a < *b)
              cmp = -1;
            else if (*a > *b)
              cmp = 1;
            a++;
            b++;
          }
        }
        if (cmp == 0) {
          if (*a)
            cmp = 1;
          else if (*b)
            cmp = -1;
        }
        if (cmp < 0) {
          char *tmp = result[j];
          result[j] = result[j + 1];
          result[j + 1] = tmp;
        }
      }
    }
  }
  return result;
}

// ======================== Forge/NeoForge 版本选择器 ========================
static char *pick_forge_version(char **versions, int count,
                                const ForgeConfig *fcfg) {
  int sel = 0, scroll = 0;
  int row, col;
  char label[32];
  snprintf(label, sizeof(label), "%s", fcfg->artifact_id);
  // 首字母大写
  if (label[0] >= 'a' && label[0] <= 'z')
    label[0] -= 32;

  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select %s version (%d available):", label, count);
    int max_disp = row - 6;
    if (sel < scroll)
      scroll = sel;
    if (sel >= scroll + max_disp)
      scroll = sel - max_disp + 1;
    if (scroll < 0)
      scroll = 0;

    for (int i = scroll; i < count && i < scroll + max_disp; i++) {
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(4 + i - scroll, 6, "%s", versions[i]);
      if (i == sel)
        attroff(A_REVERSE);
    }
    if (scroll > 0)
      mvprintw(3, 6, "... more above ...");
    if (scroll + max_disp < count)
      mvprintw(4 + max_disp, 6, "... more below ...");
    mvprintw(row - 1, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' || c == KEY_DOWN && sel < count - 1)
      sel++;
    if (c == 'k' || c == KEY_UP && sel > 0)
      sel--;
    if (c == '\n')
      break;
    if (c == 'q') {
      clear();
      return NULL;
    }
  }
  clear();
  return strdup(versions[sel]);
}

// ======================== 从 JAR 的 MANIFEST.MF 提取 Main-Class
// ========================
static int get_jar_main_class(const char *jar_path, char *out,
                              size_t out_size) {
  out[0] = '\0';
  if (!jar_path || access(jar_path, R_OK) != 0)
    return 0;

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "unzip -p '%s' META-INF/MANIFEST.MF 2>/dev/null",
           jar_path);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    // 跳过续行（以空格开头）
    if (line[0] == ' ')
      continue;
    // 匹配 "Main-Class: <classname>"
    if (strncmp(line, "Main-Class:", 11) == 0) {
      char *val = line + 11;
      // 跳过前导空格
      while (*val == ' ' || *val == '\t')
        val++;
      // 去除尾部 \r \n
      size_t vlen = strlen(val);
      while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r'))
        val[--vlen] = '\0';
      if (vlen > 0 && vlen < out_size) {
        memcpy(out, val, vlen);
        out[vlen] = '\0';
        pclose(fp);
        return 1;
      }
    }
  }
  pclose(fp);
  return 0;
}

// ======================== 解析 install_profile data 段中的值
// ======================== raw: 原始值，可能是 "{MINECRAFT_JAR}" 或
// "[group:artifact:version:classifier@ext]" 或纯字符串 data: install_profile 的
// data JSON 对象 outputs: 已执行 processor 的 outputs 累积映射 返回 malloc
// 的解析后路径/值，调用者负责 free
static char *resolve_data_value(const char *raw, const char *mcdir,
                                const char *version_dir,
                                const char *custom_name, cJSON *data,
                                cJSON *outputs) {
  if (!raw)
    return strdup("");

  // 1) 检查是否为 {KEY} 占位符
  if (raw[0] == '{') {
    const char *end = strchr(raw, '}');
    if (end) {
      size_t klen = end - raw - 1;
      char key[64] = {0};
      if (klen < sizeof(key))
        memcpy(key, raw + 1, klen);

      // ROOT 占位符 → 直接返回版本目录（data 段中 ROOT 是自引用的 {ROOT}）
      if (strcmp(key, "ROOT") == 0) {
        return strdup(version_dir);
      }

      // 先查 data 段（data 优先于 outputs，因为 outputs 中同名 key
      // 是 SHA 跟踪映射的 source key，不应覆盖 data 中的实际值）
      if (data) {
        cJSON *dv = cJSON_GetObjectItem(data, key);
        if (dv) {
          cJSON *client_val = cJSON_GetObjectItem(dv, "client");
          if (client_val && cJSON_IsString(client_val)) {
            const char *cval = client_val->valuestring;
            // 处理单引号包裹的字面值（如
            // 'de86b035d2da0f78940796bb95c39a932ed84834'）
            size_t cvlen = strlen(cval);
            if (cvlen >= 2 && cval[0] == '\'' && cval[cvlen - 1] == '\'') {
              char *unquoted = strdup(cval + 1);
              if (unquoted)
                unquoted[cvlen - 2] = '\0';
              return unquoted ? unquoted : strdup(cval);
            }
            // 防止循环：如果 data 值等于自身 key（如 "ROOT":
            // "{ROOT}"），已在上方处理
            if (strcmp(cval, raw) == 0) {
              return strdup(version_dir); // 回退到版本目录
            }
            // 值如果是 jar 内部路径（以 / 开头），需要从 installer jar 提取
            if (cval[0] == '/') {
              const char *inner = cval;
              while (*inner == '/')
                inner++;
              char tmp_path[600];
              snprintf(tmp_path, sizeof(tmp_path), "/tmp/tmcl_forge_data_%s",
                       inner);
              // 始终重新提取，避免残留旧版本文件导致校验和不匹配
              char ext_cmd[1600];
              snprintf(ext_cmd, sizeof(ext_cmd),
                       "mkdir -p \"$(dirname '%s')\" && "
                       "unzip -p '%s' '%s' > '%s' 2>/dev/null",
                       tmp_path, g_installer_path, inner, tmp_path);
              system(ext_cmd);
              return strdup(tmp_path);
            }
            // 递归解析（值可能是 [maven:coord] 格式或裸 maven 坐标）
            return resolve_data_value(cval, mcdir, version_dir, custom_name,
                                      data, outputs);
          }
        }
      }

      // 再查 outputs（processor 添加的新键，如 {MC_SLIM_SHA}）
      if (outputs) {
        cJSON *ov = cJSON_GetObjectItem(outputs, raw);
        if (ov && cJSON_IsString(ov)) {
          return strdup(ov->valuestring);
        }
      }

      // 特殊内置占位符（不在 data 段中，由 installer 运行时确定）
      if (strcmp(key, "SIDE") == 0) {
        return strdup("client"); // TMCL 始终安装 client 端
      }
      if (strcmp(key, "MINECRAFT_JAR") == 0) {
        char *result = NULL;
        asprintf(&result, "%s/%s.jar", version_dir, custom_name);
        return result ? result : strdup(version_dir);
      }
      if (strcmp(key, "INSTALLER") == 0) {
        return strdup(g_installer_path);
      }
      if (strcmp(key, "LIBRARY_DIR") == 0) {
        char *result = NULL;
        asprintf(&result, "%s/libraries", mcdir);
        return result ? result : strdup(mcdir);
      }
      if (strcmp(key, "MINECRAFT_VERSION") == 0) {
        return strdup(g_mc_version);
      }

      // 未找到：返回占位符原文
      return strdup(raw);
    }
  }

  // 2) 检查是否为 [group:artifact:version:classifier@ext] 格式
  if (raw[0] == '[' && raw[strlen(raw) - 1] == ']') {
    size_t rlen = strlen(raw);
    char *inner = malloc(rlen);
    if (!inner)
      return strdup(raw);
    memcpy(inner, raw + 1, rlen - 2);
    inner[rlen - 2] = '\0';

    char *sp;
    char *group = strtok_r(inner, ":", &sp);
    char *artifact = strtok_r(NULL, ":", &sp);
    char *version = strtok_r(NULL, ":", &sp);
    char *classifier = strtok_r(NULL, ":", &sp); // 可能为 NULL

    char *result = NULL;
    if (group && artifact && version) {
      char gp[256];
      snprintf(gp, sizeof(gp), "%s", group);
      for (char *gp2 = gp; *gp2; gp2++)
        if (*gp2 == '.')
          *gp2 = '/';

      // 特殊处理：net.minecraft:minecraft:*:client → 原版 client.jar
      if (strcmp(group, "net.minecraft") == 0 &&
          strcmp(artifact, "minecraft") == 0) {
        asprintf(&result, "%s/%s.jar", version_dir, custom_name);
      } else {
        // 普通 Maven 库
        // 处理 @ext 后缀（可能在 version 或 classifier 中）
        char *at_sign = strchr(version, '@');
        char ext[16] = "jar";
        if (at_sign) {
          snprintf(ext, sizeof(ext), "%s", at_sign + 1);
          *at_sign = '\0';
        }
        if (classifier) {
          at_sign = strchr(classifier, '@');
          if (at_sign) {
            snprintf(ext, sizeof(ext), "%s", at_sign + 1);
            *at_sign = '\0';
          }
        }

        if (classifier) {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s-%s.%s", mcdir, gp,
                   artifact, version, artifact, version, classifier, ext);
        } else {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s.%s", mcdir, gp,
                   artifact, version, artifact, version, ext);
        }
      }
    }
    free(inner);
    return result ? result : strdup(raw);
  }

  // 3) 检查是否为无括号的 Maven 坐标
  // (group:artifact:version[:classifier[@ext]]) Forge 1.20+ processor 的 "jar"
  // 字段使用此格式
  if (strchr(raw, ':')) {
    // 复制一份用于 strtok_r 修改
    char *raw_copy = strdup(raw);
    char *result = NULL;
    if (raw_copy) {
      char *sp;
      char *group = strtok_r(raw_copy, ":", &sp);
      char *artifact = strtok_r(NULL, ":", &sp);
      char *version = strtok_r(NULL, ":", &sp);
      char *classifier = strtok_r(NULL, ":", &sp);
      if (group && artifact && version) {
        char gp[256];
        snprintf(gp, sizeof(gp), "%s", group);
        for (char *p = gp; *p; p++)
          if (*p == '.')
            *p = '/';
        char *at_sign = strchr(version, '@');
        char ext[16] = "jar";
        if (at_sign) {
          snprintf(ext, sizeof(ext), "%s", at_sign + 1);
          *at_sign = '\0';
        }
        if (classifier) {
          at_sign = strchr(classifier, '@');
          if (at_sign) {
            snprintf(ext, sizeof(ext), "%s", at_sign + 1);
            *at_sign = '\0';
          }
        }
        if (classifier) {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s-%s.%s", mcdir, gp,
                   artifact, version, artifact, version, classifier, ext);
        } else {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s.%s", mcdir, gp,
                   artifact, version, artifact, version, ext);
        }
      }
      free(raw_copy);
    }
    if (result)
      return result;
  }

  // 4) 纯字符串，直接返回
  return strdup(raw);
}

// ======================== 执行单个 Forge processor ========================
static int run_forge_processor(cJSON *proc, const char *mcdir,
                               const char *version_dir, const char *custom_name,
                               cJSON *data, cJSON **outputs) {
  if (g_cancel)
    return 1;

  // 1) 检查 sides：只处理 "client"
  cJSON *sides = cJSON_GetObjectItem(proc, "sides");
  if (sides && cJSON_IsArray(sides)) {
    int has_client = 0;
    cJSON *side;
    cJSON_ArrayForEach(side, sides) {
      if (cJSON_IsString(side) && strcmp(side->valuestring, "client") == 0) {
        has_client = 1;
        break;
      }
    }
    if (!has_client)
      return 0; // 跳过 server-only processor
  }

  // 2) 获取 processor jar 路径
  cJSON *jar_name = cJSON_GetObjectItem(proc, "jar");
  if (!jar_name || !cJSON_IsString(jar_name)) {
    printf("  ERROR: processor missing 'jar' field\n");
    return 1;
  }
  char *jar_path = resolve_data_value(jar_name->valuestring, mcdir, version_dir,
                                      custom_name, data, *outputs);
  if (!jar_path || access(jar_path, R_OK) != 0) {
    printf("  ERROR: processor jar not found: %s\n",
           jar_path ? jar_path : "(null)");
    free(jar_path);
    return 1;
  }

  // 3) 提取 Main-Class
  char main_class[256] = {0};
  if (!get_jar_main_class(jar_path, main_class, sizeof(main_class))) {
    printf("  ERROR: failed to extract Main-Class from %s\n", jar_path);
    free(jar_path);
    return 1;
  }
  printf("  Processor: %s\n", main_class);

  // 4) 构建 classpath：processor jar + classpath 数组
  char classpath[32768] = {0};
  snprintf(classpath, sizeof(classpath), "%s", jar_path);

  cJSON *cp_arr = cJSON_GetObjectItem(proc, "classpath");
  if (cp_arr && cJSON_IsArray(cp_arr)) {
    cJSON *cp_item;
    cJSON_ArrayForEach(cp_item, cp_arr) {
      if (!cJSON_IsString(cp_item))
        continue;
      char *cp_path =
          resolve_data_value(cp_item->valuestring, mcdir, version_dir,
                             custom_name, data, *outputs);
      if (cp_path) {
        size_t cur = strlen(classpath);
        snprintf(classpath + cur, sizeof(classpath) - cur, ":%s", cp_path);
        free(cp_path);
      }
    }
  }

  // 5) 构建 args 数组
  char *args[256];
  int ac = 0;
  args[ac++] = "java";
  args[ac++] = "-cp";
  args[ac++] = classpath;
  args[ac++] = main_class;

  cJSON *pargs = cJSON_GetObjectItem(proc, "args");
  if (pargs && cJSON_IsArray(pargs)) {
    cJSON *arg_item;
    cJSON_ArrayForEach(arg_item, pargs) {
      if (!cJSON_IsString(arg_item) || ac >= 250)
        continue;
      char *resolved =
          resolve_data_value(arg_item->valuestring, mcdir, version_dir,
                             custom_name, data, *outputs);
      if (resolved) {
        args[ac++] = resolved;
      }
    }
  }
  args[ac] = NULL;

  // 6) 打印命令（调试用）
  printf("  CMD:");
  for (int i = 0; i < ac; i++)
    printf(" %s", args[i]);
  printf("\n");

  // 7) fork/exec
  pid_t pid = fork();
  if (pid < 0) {
    printf("  ERROR: fork failed\n");
    free(jar_path);
    // 释放 resolve_data_value 返回的 args（索引 4 开始）
    for (int i = 4; i < ac; i++)
      free(args[i]);
    return 1;
  }
  if (pid == 0) {
    execvp("java", args);
    perror("execvp java");
    _exit(1);
  }

  // 8) 等待完成，支持 SIGINT 取消
  int status, done = 0;
  while (!done) {
    if (g_cancel) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      printf("  CANCELLED\n");
      free(jar_path);
      for (int i = 4; i < ac; i++)
        free(args[i]);
      return 1;
    }
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (r > 0) {
      done = 1;
      break;
    }
    usleep(100000);
  }

  if (!done) {
    printf("  ERROR: waitpid failed\n");
    free(jar_path);
    for (int i = 4; i < ac; i++)
      free(args[i]);
    return 1;
  }

  // 9) 释放 args 中动态分配的字符串（索引 4 及以后是 resolve_data_value
  // 返回的）
  for (int i = 4; i < ac; i++)
    free(args[i]);
  free(jar_path);

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("  OK\n");
    // 10) 更新 outputs 映射
    // Forge installer 的 outputs 语义：
    //   key:   源文件占位符（如 {MC_SLIM}）
    //   value: 目标键（如 {MC_SLIM_SHA}），表示「计算 key 文件的 SHA 并存为
    //   value」
    // 我们不做 SHA 验证，只需正确映射：
    //   - 存储到 value 键（目标键）而非 key 键（源键）
    //   - 源键在 data 中有实际值，不应被覆盖
    cJSON *proc_outputs = cJSON_GetObjectItem(proc, "outputs");
    if (proc_outputs && cJSON_IsObject(proc_outputs)) {
      if (!*outputs)
        *outputs = cJSON_CreateObject();
      cJSON *child;
      cJSON_ArrayForEach(child, proc_outputs) {
        if (cJSON_IsString(child)) {
          const char *src_key = child->string; // 源占位符（如 {MC_SLIM}）
          const char *dest_key =
              child->valuestring; // 目标键（如 {MC_SLIM_SHA}）
          // 解析源文件路径
          char *src_path = resolve_data_value(src_key, mcdir, version_dir,
                                              custom_name, data, *outputs);
          if (src_path) {
            // 计算文件 SHA1（simplified: 用文件大小+mtime
            // 做标识，或直接存路径） 暂时将源路径存入目标键，以便后续 processor
            // 可以找到文件
            if (strcmp(dest_key, src_key) != 0) {
              cJSON_AddStringToObject(*outputs, dest_key, src_path);
            }
            free(src_path);
          }
        }
      }
    }
    return 0;
  } else {
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    printf("  FAILED (exit code: %d)\n", code);
    return 1;
  }
}

// ======================== 合并 vanilla + Forge version JSON
// ========================
static int merge_forge_version_json(cJSON *vanilla, cJSON *version_info,
                                    const char *custom_name,
                                    const char *mc_version,
                                    const char *modloader,
                                    const char *forge_version,
                                    const char *json_path) {
  if (!vanilla || !version_info)
    return 1;

  // a) 覆盖 mainClass（从 versionInfo 取）
  cJSON *vi_main = cJSON_GetObjectItem(version_info, "mainClass");
  if (vi_main && cJSON_IsString(vi_main)) {
    cJSON_DeleteItemFromObject(vanilla, "mainClass");
    cJSON_AddStringToObject(vanilla, "mainClass", vi_main->valuestring);
  }

  // b) 合并 arguments
  cJSON *vi_args = cJSON_GetObjectItem(version_info, "arguments");
  cJSON *v_args = cJSON_GetObjectItem(vanilla, "arguments");
  if (vi_args && v_args) {
    // JVM args — 追加 Forge JVM 参数到原版
    cJSON *vi_jvm = cJSON_GetObjectItem(vi_args, "jvm");
    cJSON *v_jvm = cJSON_GetObjectItem(v_args, "jvm");
    if (vi_jvm && cJSON_IsArray(vi_jvm) && v_jvm && cJSON_IsArray(v_jvm)) {
      cJSON *jv;
      cJSON_ArrayForEach(jv, vi_jvm) {
        cJSON_AddItemToArray(v_jvm, cJSON_Duplicate(jv, 1));
      }
    }
    // Game args — 追加 Forge game 参数
    cJSON *vi_game = cJSON_GetObjectItem(vi_args, "game");
    cJSON *v_game = cJSON_GetObjectItem(v_args, "game");
    if (vi_game && cJSON_IsArray(vi_game) && v_game && cJSON_IsArray(v_game)) {
      cJSON *gv;
      cJSON_ArrayForEach(gv, vi_game) {
        cJSON_AddItemToArray(v_game, cJSON_Duplicate(gv, 1));
      }
    }
  }

  // c) 合并 libraries（去重：同 groupId:artifactId 保留 Forge 版本）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs) && v_libs && cJSON_IsArray(v_libs)) {
    // 收集 versionInfo libraries 的 groupId:artifactId
    int vi_count = cJSON_GetArraySize(vi_libs);
    char **vi_ga = malloc(vi_count * sizeof(char *));
    if (!vi_ga && vi_count > 0)
      return 1;
    for (int i = 0; i < vi_count; i++) {
      vi_ga[i] = NULL;
      cJSON *lib = cJSON_GetArrayItem(vi_libs, i);
      cJSON *nm = cJSON_GetObjectItem(lib, "name");
      if (nm && cJSON_IsString(nm)) {
        char *ns = strdup(nm->valuestring);
        char *first_colon = strchr(ns, ':');
        if (first_colon) {
          char *second_colon = strchr(first_colon + 1, ':');
          if (second_colon)
            *second_colon = '\0';
        }
        vi_ga[i] = ns;
      }
    }
    // 移除原版中重复的 library（从后往前遍历）
    int v_count = cJSON_GetArraySize(v_libs);
    for (int i = v_count - 1; i >= 0; i--) {
      cJSON *lib = cJSON_GetArrayItem(v_libs, i);
      cJSON *nm = cJSON_GetObjectItem(lib, "name");
      if (nm && cJSON_IsString(nm)) {
        char *ns = strdup(nm->valuestring);
        char *fc = strchr(ns, ':');
        if (fc) {
          char *sc = strchr(fc + 1, ':');
          if (sc)
            *sc = '\0';
        }
        for (int j = 0; j < vi_count; j++) {
          if (vi_ga[j] && strcmp(ns, vi_ga[j]) == 0) {
            cJSON_DeleteItemFromArray(v_libs, i);
            break;
          }
        }
        free(ns);
      }
    }
    for (int i = 0; i < vi_count; i++)
      free(vi_ga[i]);
    free(vi_ga);
    // 追加 versionInfo libraries
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      cJSON_AddItemToArray(v_libs, cJSON_Duplicate(lib, 1));
    }
  }

  // d) 移除 inheritsFrom
  cJSON_DeleteItemFromObject(vanilla, "inheritsFrom");

  // e) 设置 id
  cJSON_DeleteItemFromObject(vanilla, "id");
  cJSON_AddStringToObject(vanilla, "id", custom_name);

  // f) 添加元数据（init_versions 用 forge/neoforge + gameVersion 字段检测）
  cJSON_AddStringToObject(vanilla, "gameVersion", mc_version);
  if (strcmp(modloader, "neoforge") == 0) {
    cJSON *neo_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(neo_meta, "version", forge_version);
    cJSON_AddItemToObject(vanilla, "neoforge", neo_meta);
  } else {
    cJSON *forge_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(forge_meta, "version", forge_version);
    cJSON_AddItemToObject(vanilla, "forge", forge_meta);
  }

  // g) 写回文件
  char *merged = cJSON_Print(vanilla);
  if (!merged)
    return 1;
  FILE *mf = fopen(json_path, "w");
  if (!mf) {
    free(merged);
    return 1;
  }
  fputs(merged, mf);
  fclose(mf);
  free(merged);
  printf("  OK  merged %s + vanilla JSON\n", modloader);
  return 0;
}

static void do_install(ConfigState *cstate, const char *version_id,
                       const char *version_url, const char *custom_name,
                       const char *modloader, const char *type,
                       const char *fabric_loader_ver) {
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1)
    num_threads = 96;
  int asset_threads = atoi(cstate->items[7].value);
  if (asset_threads < 1)
    asset_threads = 96;
  char *mcdir = cstate->items[2].value;

  // Fabric 安装：使用用户选择的 loader 版本构建 profile JSON URL
  char *fabric_url = NULL;
  if (strcmp(modloader, "fabric") == 0) {
    if (!fabric_loader_ver || !fabric_loader_ver[0]) {
      printf("ERROR: No Fabric loader version selected for %s\n", version_id);
      goto done;
    }
    asprintf(&fabric_url,
             "https://meta.fabricmc.net/v2/versions/loader/%s/%s/profile/json",
             version_id, fabric_loader_ver);
  }

  printf("\n=== Installing %s (%s) ===\n", custom_name, version_id);
  printf("    Threads: %d (libs) / %d (assets)\n\n", num_threads,
         asset_threads);

  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  // 1) 下载原版版本 JSON（作为基础）
  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);
  if (dl_one(version_url, json_path, "version JSON") != 0) {
    printf("\nFailed to download version JSON. Aborting.\n");
    free(fabric_url);
    goto done;
  }

  // 对于 Fabric：下载 Fabric profile JSON，合并到原版 JSON 中，
  // 生成自包含的 JSON（CMCL 风格：无 inheritsFrom，mainClass 直接指向 Fabric）
  if (strcmp(modloader, "fabric") == 0) {
    // loader 版本已由用户在安装前选择
    char loader_ver[64];
    snprintf(loader_ver, sizeof(loader_ver), "%s",
             fabric_loader_ver ? fabric_loader_ver : "unknown");

    // 下载 Fabric profile JSON 到内存
    char cmd[1280];
    snprintf(cmd, sizeof(cmd), "curl -sL '%s'", fabric_url);
    FILE *fp = popen(cmd, "r");
    char *fabric_raw = NULL;
    size_t fcap = 0, flen = 0;
    if (fp) {
      char chunk[4096];
      while (fgets(chunk, sizeof(chunk), fp)) {
        size_t cl = strlen(chunk);
        if (flen + cl + 1 > fcap) {
          fcap = fcap ? fcap * 2 : 4096;
          fabric_raw = realloc(fabric_raw, fcap);
        }
        memcpy(fabric_raw + flen, chunk, cl);
        flen += cl;
        fabric_raw[flen] = '\0';
      }
      pclose(fp);
    }
    free(fabric_url);
    fabric_url = NULL;

    cJSON *fprofile = NULL;
    if (fabric_raw) {
      fprofile = cJSON_Parse(fabric_raw);
      free(fabric_raw);
    }
    if (!fprofile) {
      printf("ERROR: Failed to parse Fabric profile JSON.\n");
      goto done;
    }

    // 读取已下载的原版 JSON
    cJSON *vanilla = NULL;
    FILE *vf = fopen(json_path, "r");
    if (vf) {
      fseek(vf, 0, SEEK_END);
      long vsz = ftell(vf);
      fseek(vf, 0, SEEK_SET);
      char *vd = malloc(vsz + 1);
      if (vd) {
        fread(vd, 1, vsz, vf);
        vd[vsz] = '\0';
        vanilla = cJSON_Parse(vd);
        free(vd);
      }
      fclose(vf);
    }
    if (!vanilla) {
      printf("ERROR: Failed to parse vanilla JSON.\n");
      cJSON_Delete(fprofile);
      goto done;
    }

    // --- 合并操作 ---
    // a) 改 mainClass
    cJSON *fb_main = cJSON_GetObjectItem(fprofile, "mainClass");
    if (fb_main && cJSON_IsString(fb_main)) {
      cJSON_DeleteItemFromObject(vanilla, "mainClass");
      cJSON_AddStringToObject(vanilla, "mainClass", fb_main->valuestring);
    }

    // b) 去重 + 追加 Fabric libraries
    // Fabric 带了自己的 ASM 等库，如果原版已有同名 library（同
    // groupId:artifactId）， 移除原版的旧版本，否则 Fabric Loader
    // 检测到重复类会拒绝启动
    cJSON *fb_libs = cJSON_GetObjectItem(fprofile, "libraries");
    cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
    if (fb_libs && cJSON_IsArray(fb_libs) && v_libs && cJSON_IsArray(v_libs)) {
      // 先收集 Fabric libraries 的 groupId:artifactId
      int fb_count = cJSON_GetArraySize(fb_libs);
      char **fb_ga = malloc(fb_count * sizeof(char *));
      for (int i = 0; i < fb_count; i++) {
        fb_ga[i] = NULL;
        cJSON *lib = cJSON_GetArrayItem(fb_libs, i);
        cJSON *nm = cJSON_GetObjectItem(lib, "name");
        if (nm && cJSON_IsString(nm)) {
          char *ns = strdup(nm->valuestring);
          char *first_colon = strchr(ns, ':');
          if (first_colon) {
            char *second_colon = strchr(first_colon + 1, ':');
            if (second_colon)
              *second_colon = '\0'; // 截断到 groupId:artifactId
          }
          fb_ga[i] = ns;
        }
      }
      // 移除原版中重复的 library（从后往前遍历以免索引错位）
      int v_count = cJSON_GetArraySize(v_libs);
      for (int i = v_count - 1; i >= 0; i--) {
        cJSON *lib = cJSON_GetArrayItem(v_libs, i);
        cJSON *nm = cJSON_GetObjectItem(lib, "name");
        if (nm && cJSON_IsString(nm)) {
          char *ns = strdup(nm->valuestring);
          char *fc = strchr(ns, ':');
          if (fc) {
            char *sc = strchr(fc + 1, ':');
            if (sc)
              *sc = '\0';
          }
          for (int j = 0; j < fb_count; j++) {
            if (fb_ga[j] && strcmp(ns, fb_ga[j]) == 0) {
              cJSON_DeleteItemFromArray(v_libs, i);
              break;
            }
          }
          free(ns);
        }
      }
      for (int i = 0; i < fb_count; i++)
        free(fb_ga[i]);
      free(fb_ga);
      // 追加 Fabric libraries
      cJSON *lib;
      cJSON_ArrayForEach(lib, fb_libs) {
        cJSON_AddItemToArray(v_libs, cJSON_Duplicate(lib, 1));
      }
    }

    // c) 合并 arguments（Fabric 可能有额外的 JVM 参数，跳过 -DFabric.*）
    cJSON *fb_args = cJSON_GetObjectItem(fprofile, "arguments");
    cJSON *v_args = cJSON_GetObjectItem(vanilla, "arguments");
    if (fb_args && v_args) {
      cJSON *fb_jvm = cJSON_GetObjectItem(fb_args, "jvm");
      cJSON *v_jvm = cJSON_GetObjectItem(v_args, "jvm");
      if (fb_jvm && cJSON_IsArray(fb_jvm) && v_jvm && cJSON_IsArray(v_jvm)) {
        cJSON *jv;
        cJSON_ArrayForEach(jv, fb_jvm) {
          if (cJSON_IsString(jv)) {
            if (strncmp(jv->valuestring, "-DFabric", 8) == 0)
              continue;
          }
          cJSON_AddItemToArray(v_jvm, cJSON_Duplicate(jv, 1));
        }
      }
    }

    // d) 添加元数据（init_versions 用 fabric / gameVersion 字段检测）
    cJSON_AddStringToObject(vanilla, "gameVersion", version_id);
    cJSON *fabric_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(fabric_meta, "loaderVersion",
                            loader_ver[0] ? loader_ver : "unknown");
    cJSON_AddItemToObject(vanilla, "fabric", fabric_meta);

    // e) 移除 inheritsFrom（现在是自包含 JSON）
    cJSON_DeleteItemFromObject(vanilla, "inheritsFrom");

    // f) 改 id
    cJSON_DeleteItemFromObject(vanilla, "id");
    cJSON_AddStringToObject(vanilla, "id", custom_name);

    // 写回合并后的 JSON
    char *merged = cJSON_Print(vanilla);
    if (merged) {
      FILE *mf = fopen(json_path, "w");
      if (mf) {
        fputs(merged, mf);
        fclose(mf);
      }
      free(merged);
      printf("  OK  merged Fabric + vanilla JSON\n");
    }

    cJSON_Delete(vanilla);
    cJSON_Delete(fprofile);
  } else {
    free(fabric_url);
  }
  fabric_url = NULL;

  // 2) 解析版本 JSON（原版为直接下载的，Fabric 为合并后的自包含 JSON）
  FILE *jf = fopen(json_path, "r");
  if (!jf) {
    printf("Failed to read version JSON.\n");
    goto done;
  }
  fseek(jf, 0, SEEK_END);
  long jsz = ftell(jf);
  fseek(jf, 0, SEEK_SET);
  char *jdata = malloc(jsz + 1);
  fread(jdata, 1, jsz, jf);
  jdata[jsz] = '\0';
  fclose(jf);

  cJSON *root = cJSON_Parse(jdata);
  free(jdata);
  if (!root) {
    printf("Failed to parse version JSON.\n");
    goto done;
  }

  // ---- Phase 1: 收集所有下载任务 ----
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  int tcount = 0;
  // 8192 slots is far beyond practical max (~200 files), so no bounds check
  // needed

  // a) client.jar（合并后的 JSON 自带 downloads.client）
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
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                 "client.jar");
        tcount++;
      }
    }
  }

  // b) libraries（合并后的 JSON 已包含 vanilla + Fabric 全部 libraries）
  cJSON *libraries = cJSON_GetObjectItem(root, "libraries");
  int lib_match = 0;
  if (libraries && cJSON_IsArray(libraries)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, libraries) {
      cJSON *rules = cJSON_GetObjectItem(lib, "rules");
      if (lib_rules_ok(rules))
        lib_match++;
    }

    int cur = 0;
    cJSON_ArrayForEach(lib, libraries) {
      cJSON *rules = cJSON_GetObjectItem(lib, "rules");
      if (!lib_rules_ok(rules))
        continue;
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

  // c) asset index（合并后的 JSON 自带 assetIndex）
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
  dl_parallel(tasks, tcount, num_threads);

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
    int f = retry_failed(tasks, tcount, 8, &failed_names, &failed_count);
    if (f == 0)
      break;
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
      if (ai_data)
        ai_root = cJSON_Parse(ai_data);
      if (ai_root) {
        cJSON *objects = cJSON_GetObjectItem(ai_root, "objects");
        if (objects && cJSON_IsObject(objects)) {
          // 第一遍：统计需要下载的 asset 数量
          int need_dl = 0, already = 0;
          cJSON *obj_item;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item))
              continue;
            const char *hash = hash_item->valuestring;
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%c%c/%s", mcdir,
                     hash[0], hash[1], hash);
            if (access(dest, F_OK) == 0)
              already++;
            else
              need_dl++;
          }

          DlTask *atasks = malloc((need_dl + 1) * sizeof(DlTask));
          int ac = 0;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item))
              continue;
            const char *hash = hash_item->valuestring;
            char first2[3] = {hash[0], hash[1], '\0'};
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%s/%s", mcdir,
                     first2, hash);
            if (access(dest, F_OK) == 0)
              continue;

            snprintf(atasks[ac].url, sizeof(atasks[ac].url),
                     "https://resources.download.minecraft.net/%s/%s", first2,
                     hash);
            // path may be up to 600 bytes; DlTask.dest is 512 — use strncpy
            // with explicit truncation guard
            strncpy(atasks[ac].dest, dest, sizeof(atasks[ac].dest) - 1);
            atasks[ac].dest[sizeof(atasks[ac].dest) - 1] = '\0';
            snprintf(atasks[ac].label, sizeof(atasks[ac].label), "[%d/%d] %s",
                     ac + 1, need_dl, obj_item->string);
            ac++;
          }
          if (ac > 0) {
            printf("\n  Assets: %d need download (%d already cached)\n\n", ac,
                   already);
            dl_parallel(atasks, ac, asset_threads);
            // 对 asset 也做重试
            char **a_fail = NULL;
            int a_fc = 0;
            for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
              int f = retry_failed(atasks, ac, asset_threads, &a_fail, &a_fc);
              if (f == 0)
                break;
              // 合并失败列表
              if (a_fc > 0) {
                failed_names = realloc(failed_names,
                                       (failed_count + a_fc) * sizeof(char *));
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
  if (g_cancel)
    printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {
  }

done:
  if (root)
    cJSON_Delete(root);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}

// ======================== 旧版 Forge 安装主函数（1.7.10–1.12.2）
// ========================
void do_legacy_forge_install(ConfigState *cstate, const char *mc_version,
                             const char *vanilla_url, const char *custom_name,
                             const char *modloader, const char *type,
                             const ForgeConfig *fcfg,
                             const char *forge_version) {
  (void)modloader;
  (void)type;
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1)
    num_threads = 96;
  char *mcdir = cstate->items[2].value;

  int asset_threads = atoi(cstate->items[7].value);
  if (asset_threads < 1)
    asset_threads = 96;

  printf("\n=== Installing %s (%s / forge legacy) ===\n", custom_name,
         mc_version);
  printf("    Threads: %d (libs) / %d (assets)\n\n", num_threads,
         asset_threads);

  // 创建版本目录
  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);

  cJSON *vanilla =
      NULL; // 原版 JSON，Phase 2 解析，Phase 4 合并，clean_tasks 释放

  // ---- Phase 0: 确认 forge 版本（已在 install_page 中预选） ----
  char *chosen_forge_ver = NULL;
  if (forge_version && forge_version[0]) {
    chosen_forge_ver = strdup(forge_version);
  }
  if (!chosen_forge_ver) {
    printf("ERROR: No forge version specified.\n");
    goto done;
  }
  printf("  Forge version: %s (legacy)\n", chosen_forge_ver);

  // 构建 Maven 路径
  char group_path[256];
  snprintf(group_path, sizeof(group_path), "%s", fcfg->group_id);
  for (char *p = group_path; *p; p++)
    if (*p == '.')
      *p = '/';

  // ---- Phase 1: 下载 installer jar 并提取 install_profile.json ----
  char installer_url[1024];
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar", fcfg->maven_base,
           group_path, fcfg->artifact_id, chosen_forge_ver, fcfg->artifact_id,
           chosen_forge_ver);

  char installer_path[600];
  snprintf(installer_path, sizeof(installer_path),
           "/tmp/tmcl_forge_legacy_installer_%s.jar", chosen_forge_ver);
  printf("  Downloading installer...\n");
  if (dl_one(installer_url, installer_path, "installer jar") != 0) {
    printf("ERROR: Failed to download installer jar.\n");
    goto done;
  }

  // 提取 install_profile.json
  printf("  Extracting install_profile.json...\n");
  char extract_cmd[1536];
  snprintf(extract_cmd, sizeof(extract_cmd),
           "unzip -p '%s' install_profile.json 2>/dev/null", installer_path);
  FILE *ep = popen(extract_cmd, "r");
  if (!ep) {
    printf("ERROR: Failed to extract install_profile.json.\n");
    remove(installer_path);
    goto done;
  }

  char *profile_raw = NULL;
  size_t pcap = 0, plen = 0;
  {
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), ep)) {
      size_t cl = strlen(chunk);
      if (plen + cl + 1 > pcap) {
        pcap = pcap ? pcap * 2 : 16384;
        void *newbuf = realloc(profile_raw, pcap);
        if (!newbuf) {
          pclose(ep);
          free(profile_raw);
          goto done;
        }
        profile_raw = newbuf;
      }
      memcpy(profile_raw + plen, chunk, cl);
      plen += cl;
      profile_raw[plen] = '\0';
    }
  }
  pclose(ep);
  remove(installer_path);

  if (!profile_raw) {
    printf("ERROR: install_profile.json is empty.\n");
    goto done;
  }

  cJSON *install_profile = cJSON_Parse(profile_raw);
  free(profile_raw);
  if (!install_profile) {
    printf("ERROR: Failed to parse install_profile.json.\n");
    goto done;
  }

  cJSON *version_info = cJSON_GetObjectItem(install_profile, "versionInfo");
  if (!version_info) {
    printf("ERROR: install_profile.json missing 'versionInfo'.\n");
    cJSON_Delete(install_profile);
    goto done;
  }

  // ---- Phase 2: 下载原版 JSON（仅用于解析，不写入 versions 目录） ----
  printf("  Downloading vanilla JSON for %s...\n", mc_version);
  {
    char tmp_vanilla[600];
    snprintf(tmp_vanilla, sizeof(tmp_vanilla), "/tmp/tmcl_vanilla_%s.json",
             mc_version);
    if (dl_one(vanilla_url, tmp_vanilla, "vanilla JSON") != 0) {
      printf("ERROR: Failed to download vanilla JSON.\n");
      cJSON_Delete(install_profile);
      goto done;
    }

    FILE *vf = fopen(tmp_vanilla, "r");
    if (!vf) {
      remove(tmp_vanilla);
      cJSON_Delete(install_profile);
      goto done;
    }
    fseek(vf, 0, SEEK_END);
    long vsz = ftell(vf);
    rewind(vf);
    char *vdata = malloc(vsz + 1);
    if (!vdata) {
      fclose(vf);
      remove(tmp_vanilla);
      cJSON_Delete(install_profile);
      goto done;
    }
    fread(vdata, 1, vsz, vf);
    fclose(vf);
    vdata[vsz] = '\0';
    remove(tmp_vanilla);

    vanilla = cJSON_Parse(vdata);
    free(vdata);
    if (!vanilla) {
      printf("ERROR: Failed to parse vanilla JSON.\n");
      cJSON_Delete(install_profile);
      goto done;
    }
  }

  // ---- Phase 3: 收集并下载所有文件 ----

  // 构建下载任务数组
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  if (!tasks) {
    cJSON_Delete(vanilla);
    cJSON_Delete(install_profile);
    goto done;
  }
  int tcount = 0;

  // a) 原版 libraries（从 vanilla JSON）
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  if (v_libs && cJSON_IsArray(v_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, v_libs) {
      if (!lib_rules_ok(cJSON_GetObjectItem(lib, "rules")))
        continue;
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir))
        tcount++;
      // 处理旧版 MC（1.7.10 等）的 downloads.classifiers 原生库：
      // 原版 JSON 中，原生库使用 downloads.classifiers 指定各平台的 jar，
      // 而非在 name/maven 坐标中包含 classifier。需为 native jars
      // 创建下载任务。
      {
        cJSON *dl = cJSON_GetObjectItem(lib, "downloads");
        cJSON *clsfrs = dl ? cJSON_GetObjectItem(dl, "classifiers") : NULL;
        if (clsfrs && cJSON_IsObject(clsfrs)) {
          cJSON *clsfr = clsfrs->child;
          while (clsfr) {
            if (cJSON_IsObject(clsfr) &&
                strncmp(clsfr->string, "natives-", 8) == 0) {
              cJSON *cl_path = cJSON_GetObjectItem(clsfr, "path");
              cJSON *cl_url = cJSON_GetObjectItem(clsfr, "url");
              if (cl_path && cJSON_IsString(cl_path) && cl_url &&
                  cJSON_IsString(cl_url)) {
                DlTask *nt = &tasks[tcount];
                snprintf(nt->dest, sizeof(nt->dest), "%s/libraries/%s", mcdir,
                         cl_path->valuestring);
                snprintf(nt->url, sizeof(nt->url), "%s", cl_url->valuestring);
                snprintf(nt->label, sizeof(nt->label), "[natives] %s",
                         cl_path->valuestring);
                tcount++;
              }
            }
            clsfr = clsfr->next;
          }
        }
      }
    }
  }
  int vanilla_lib_count = tcount;

  // b) Forge libraries（从 install_profile 的 versionInfo.libraries）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      // 旧版 Forge 的 universal jar 在 Maven 上带有 -universal 后缀，
      // 但 install_profile.json 的 library name 可能不含 :universal classifier
      {
        cJSON *nm = cJSON_GetObjectItem(lib, "name");
        if (nm && cJSON_IsString(nm)) {
          const char *ns = nm->valuestring;
          int colons = 0;
          for (const char *p = ns; *p; p++)
            if (*p == ':')
              colons++;
          if (colons == 2 &&
              strncmp(ns, "net.minecraftforge:forge:", 24) == 0) {
            char newname[256];
            snprintf(newname, sizeof(newname), "%s:universal", ns);
            cJSON_ReplaceItemInObject(lib, "name", cJSON_CreateString(newname));
          }
        }
      }
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[forge] %s", t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }
  int forge_lib_count = tcount - vanilla_lib_count;
  printf("  Libraries: %d vanilla + %d forge = %d total\n", vanilla_lib_count,
         forge_lib_count, tcount);

  // c) 原版 client.jar（放到当前版本目录，命名为 custom_name.jar）
  {
    char client_jar_path[600];
    snprintf(client_jar_path, sizeof(client_jar_path), "%s/%s.jar", version_dir,
             custom_name);
    cJSON *downloads = cJSON_GetObjectItem(vanilla, "downloads");
    if (downloads) {
      cJSON *client = cJSON_GetObjectItem(downloads, "client");
      if (client) {
        cJSON *jar_url = cJSON_GetObjectItem(client, "url");
        if (jar_url && cJSON_IsString(jar_url)) {
          snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                   jar_url->valuestring);
          snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s",
                   client_jar_path);
          snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                   "vanilla client.jar");
          tcount++;
        }
      }
    }
  }

  // d) Asset index
  char ai_path[512] = {0};
  {
    cJSON *assetIndex = cJSON_GetObjectItem(vanilla, "assetIndex");
    if (assetIndex) {
      cJSON *ai_u = cJSON_GetObjectItem(assetIndex, "url");
      cJSON *ai_id = cJSON_GetObjectItem(assetIndex, "id");
      if (ai_u && cJSON_IsString(ai_u) && ai_id && cJSON_IsString(ai_id)) {
        snprintf(ai_path, sizeof(ai_path), "%s/assets/indexes/%s.json", mcdir,
                 ai_id->valuestring);
        snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                 ai_u->valuestring);
        snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s", ai_path);
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                 "asset index");
        tcount++;
      }
    }
  }

  // ---- Phase 3b: 并行下载 ----
  printf("  Downloading %d files...\n\n", tcount);
  dl_parallel(tasks, tcount, 8);

  // 重试失败
  char **failed_names = NULL;
  int failed_count = 0;
  if (g_cancel) {
    printf("\n  Cancelled by user, skipping retries.\n");
    goto clean_tasks;
  }
  for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
    int f = retry_failed(tasks, tcount, 8, &failed_names, &failed_count);
    if (f == 0)
      break;
  }

  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
    failed_names = NULL;
    failed_count = 0;
  }
  // ---- Phase 3c: 提取原生库 (.so/.dll/.dylib) 到 versions/<name>/natives/
  // ----
  {
    char ndir[512];
    snprintf(ndir, sizeof(ndir), "%s/natives", version_dir);
    mkdir(ndir, 0755);
    int extracted = 0;
    for (int i = 0; i < tcount; i++) {
      if (tasks[i].result != 0)
        continue;
      if (strstr(tasks[i].dest, "natives-") != NULL) {
        char cmd[2048];
        snprintf(
            cmd, sizeof(cmd),
            "unzip -q -j -o '%s' '*.so' '*.dll' '*.dylib' -d '%s' 2>/dev/null",
            tasks[i].dest, ndir);
        if (!extracted) {
          printf("  Extracting native libraries...\n");
          extracted = 1;
        }
        printf("    %s\n", tasks[i].label);
        if (system(cmd) != 0) {
          printf("    WARNING: native extraction failed for %s\n",
                 tasks[i].label);
        }
      }
    }
  }

  // ---- Phase 4: 合并原版 + Forge 生成自包含版本 JSON（无 inheritsFrom） ----
  if (!g_cancel) {
    printf("\n  Writing self-contained version JSON...\n");
    // 以原版 vanilla JSON 为基础
    cJSON_ReplaceItemInObject(vanilla, "id", cJSON_CreateString(custom_name));
    // 从 forge versionInfo 覆盖 mainClass
    {
      cJSON *fmc = cJSON_GetObjectItem(version_info, "mainClass");
      if (fmc && cJSON_IsString(fmc)) {
        cJSON_ReplaceItemInObject(vanilla, "mainClass",
                                  cJSON_CreateString(fmc->valuestring));
      }
    }
    // 从 forge versionInfo 覆盖 minecraftArguments
    {
      cJSON *fma = cJSON_GetObjectItem(version_info, "minecraftArguments");
      if (fma && cJSON_IsString(fma)) {
        cJSON_ReplaceItemInObject(vanilla, "minecraftArguments",
                                  cJSON_CreateString(fma->valuestring));
      }
    }
    // 合并 libraries：forge 优先，按 group:artifact 去重
    {
      cJSON *vlibs = cJSON_GetObjectItem(vanilla, "libraries");
      cJSON *flibs = cJSON_GetObjectItem(version_info, "libraries");
      if (flibs && cJSON_IsArray(flibs) && vlibs && cJSON_IsArray(vlibs)) {
        // 1) 先从 forge 库列表构建新数组（forge 版本优先）
        cJSON *merged = cJSON_Duplicate(flibs, 1);
        // 2) 添加原版库中与 forge 不冲突的（按 group:artifact 匹配）
        cJSON *vl;
        cJSON_ArrayForEach(vl, vlibs) {
          cJSON *vn = cJSON_GetObjectItem(vl, "name");
          if (!vn || !cJSON_IsString(vn))
            continue;
          // 提取 group:artifact（忽略版本）
          const char *vfull = vn->valuestring;
          const char *last_colon = strrchr(vfull, ':');
          if (!last_colon)
            continue;
          size_t ga_len = (size_t)(last_colon - vfull);
          // 检查是否与任何 forge 库的 group:artifact 冲突
          int dup = 0;
          cJSON *fl;
          cJSON_ArrayForEach(fl, flibs) {
            cJSON *fn = cJSON_GetObjectItem(fl, "name");
            if (!fn || !cJSON_IsString(fn))
              continue;
            const char *ffull = fn->valuestring;
            const char *flast = strrchr(ffull, ':');
            if (flast && (size_t)(flast - ffull) == ga_len &&
                strncmp(vfull, ffull, ga_len) == 0) {
              dup = 1;
              break;
            }
          }
          if (!dup) {
            cJSON_AddItemToArray(merged, cJSON_Duplicate(vl, 1));
          }
        }
        // 3) 替换原版 libraries 为合并结果
        cJSON_ReplaceItemInObject(vanilla, "libraries", merged);
      }
    }
    // 移除 inheritsFrom
    cJSON_DeleteItemFromObject(vanilla, "inheritsFrom");
    // 添加 forge 元数据
    {
      cJSON *forge_meta = cJSON_CreateObject();
      cJSON_AddStringToObject(forge_meta, "version", chosen_forge_ver);
      cJSON_AddItemToObject(vanilla, "forge", forge_meta);
    }
    // 添加 gameVersion 供 init_versions 版本扫描时检测
    cJSON_AddStringToObject(vanilla, "gameVersion", mc_version);
    // 写入文件
    char *json_str = cJSON_Print(vanilla);
    if (json_str) {
      FILE *jf = fopen(json_path, "w");
      if (jf) {
        fprintf(jf, "%s", json_str);
        fclose(jf);
      }
      free(json_str);
    }
  }

  // ---- Phase 5: 下载 assets ----
  if (ai_path[0] && !g_cancel) {
    FILE *aif = fopen(ai_path, "r");
    if (aif) {
      fseek(aif, 0, SEEK_END);
      long asz = ftell(aif);
      rewind(aif);
      char *ai_data = malloc(asz + 1);
      if (ai_data) {
        fread(ai_data, 1, asz, aif);
        ai_data[asz] = '\0';
      }
      fclose(aif);

      cJSON *ai_root = ai_data ? cJSON_Parse(ai_data) : NULL;
      free(ai_data);
      if (ai_root) {
        cJSON *objects = cJSON_GetObjectItem(ai_root, "objects");
        if (objects && cJSON_IsObject(objects)) {
          int need_dl = 0, already = 0;
          cJSON *obj_item;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item))
              continue;
            const char *hash = hash_item->valuestring;
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%c%c/%s", mcdir,
                     hash[0], hash[1], hash);
            if (access(dest, F_OK) == 0)
              already++;
            else
              need_dl++;
          }

          DlTask *atasks = malloc((need_dl + 1) * sizeof(DlTask));
          if (atasks) {
            int ac = 0;
            cJSON_ArrayForEach(obj_item, objects) {
              cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
              if (!hash_item || !cJSON_IsString(hash_item))
                continue;
              const char *hash = hash_item->valuestring;
              char first2[3] = {hash[0], hash[1], '\0'};
              char dest[600];
              snprintf(dest, sizeof(dest), "%s/assets/objects/%s/%s", mcdir,
                       first2, hash);
              if (access(dest, F_OK) == 0)
                continue;
              snprintf(atasks[ac].url, sizeof(atasks[ac].url),
                       "https://resources.download.minecraft.net/%s/%s", first2,
                       hash);
              snprintf(atasks[ac].dest, sizeof(atasks[ac].dest), "%s", dest);
              snprintf(atasks[ac].label, sizeof(atasks[ac].label), "[%d/%d] %s",
                       ac + 1, need_dl, obj_item->string);
              ac++;
            }
            if (ac > 0) {
              printf("\n  Assets: %d need download (%d already cached)\n\n", ac,
                     already);
              dl_parallel(atasks, ac, num_threads);
              for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
                char **a_fail = NULL;
                int a_fc = 0;
                int f = retry_failed(atasks, ac, num_threads, &a_fail, &a_fc);
                if (f == 0)
                  break;
                if (a_fc > 0) {
                  failed_names = realloc(failed_names, (failed_count + a_fc) *
                                                           sizeof(char *));
                  if (failed_names) {
                    for (int i = 0; i < a_fc; i++)
                      failed_names[failed_count++] = a_fail[i];
                  }
                  free(a_fail);
                }
              }
            } else {
              printf("\n  Assets: all %d already cached\n", already);
            }
            free(atasks);
          }
        }
        cJSON_Delete(ai_root);
      }
    }
  }

clean_tasks:
  cJSON_Delete(vanilla);
  free(tasks);

  // 打印失败总结
  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
  }

  printf("\n=== Installation complete: %s ===\n", custom_name);
  if (g_cancel)
    printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {
  }

done:
  cJSON_Delete(install_profile);
  free(chosen_forge_ver);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}

// ======================== Forge/NeoForge 安装主函数 ========================
//
// 未来工作——BMCLAPI 镜像支持：
// 当用户配置的 download_cource（cstate->items[4]）为 "BMCLAPI" 时，
// 应将 Maven base 替换为 https://bmclapi2.bangbang93.com/maven/ 以加速下载。
// 当前使用官方 Maven（maven.minecraftforge.net / maven.neoforged.net），
// 对于中国大陆用户可能较慢，后续可参照普通安装中 BMCLAPI 下载源的实现方式。
void do_forge_install(ConfigState *cstate, const char *mc_version,
                      const char *vanilla_url, const char *custom_name,
                      const char *modloader, const char *type,
                      const ForgeConfig *fcfg, const char *forge_version) {
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1)
    num_threads = 96;
  char *mcdir = cstate->items[2].value;

  int asset_threads = atoi(cstate->items[7].value);
  if (asset_threads < 1)
    asset_threads = 96;

  printf("\n=== Installing %s (%s / %s) ===\n", custom_name, mc_version,
         fcfg->artifact_id);
  printf("    Threads: %d (libs) / %d (assets)\n\n", num_threads,
         asset_threads);

  // 创建版本目录
  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);

  // 构建 Maven 路径：将 group_id 中的 . 替换为 /（供 Phase 0/1 使用）
  char group_path[256];
  snprintf(group_path, sizeof(group_path), "%s", fcfg->group_id);
  for (char *p = group_path; *p; p++)
    if (*p == '.')
      *p = '/';

  // ---- Phase 0: 确定 Forge/NeoForge 版本 ----
  char *chosen_forge_ver = NULL;
  if (forge_version && forge_version[0]) {
    // 版本已在 install_page 中预先选定
    chosen_forge_ver = strdup(forge_version);
    if (!chosen_forge_ver)
      goto done;
    printf("  %s version: %s (pre-selected)\n", fcfg->artifact_id,
           chosen_forge_ver);
  } else {
    // 拉取 Maven metadata，让用户选择版本
    char meta_url[1024];
    snprintf(meta_url, sizeof(meta_url), "https://%s/%s/%s/maven-metadata.xml",
             fcfg->maven_base, group_path, fcfg->artifact_id);

    printf("  Fetching %s versions...\n", fcfg->artifact_id);
    char cmd[1536];
    snprintf(cmd, sizeof(cmd), "curl -sL -o /tmp/tmcl_forge_meta.xml '%s'",
             meta_url);
    if (system(cmd) != 0) {
      printf("ERROR: Failed to fetch Maven metadata.\n");
      goto done;
    }

    FILE *mf = fopen("/tmp/tmcl_forge_meta.xml", "r");
    if (!mf) {
      printf("ERROR: Failed to read Maven metadata.\n");
      goto done;
    }
    fseek(mf, 0, SEEK_END);
    long msz = ftell(mf);
    fseek(mf, 0, SEEK_SET);
    char *mdata = malloc(msz + 1);
    if (!mdata) {
      fclose(mf);
      goto done;
    }
    fread(mdata, 1, msz, mf);
    mdata[msz] = '\0';
    fclose(mf);

    int fver_count = 0;
    char **fversions = parse_maven_xml(
        mdata, mc_version, &fver_count,
        /*neoforge_mode=*/(strcmp(modloader, "neoforge") == 0), type);
    free(mdata);
    remove("/tmp/tmcl_forge_meta.xml");
    if (!fversions || fver_count == 0) {
      printf("ERROR: No %s versions found for MC %s\n", fcfg->artifact_id,
             mc_version);
      goto done;
    }

    chosen_forge_ver = pick_forge_version(fversions, fver_count, fcfg);
    for (int i = 0; i < fver_count; i++)
      free(fversions[i]);
    free(fversions);
    if (!chosen_forge_ver) {
      printf("  Cancelled by user.\n");
      goto done;
    }
    printf("  Selected: %s %s\n", fcfg->artifact_id, chosen_forge_ver);
  }

  // ---- Phase 1: 下载 installer jar 并提取 install_profile.json ----
  char installer_url[1024];
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar", fcfg->maven_base,
           group_path, fcfg->artifact_id, chosen_forge_ver, fcfg->artifact_id,
           chosen_forge_ver);

  char installer_path[600];
  snprintf(installer_path, sizeof(installer_path),
           "/tmp/tmcl_forge_installer_%s.jar", chosen_forge_ver);
  // 保存到全局变量，供 resolve_data_value 的 {INSTALLER} / jar 内部文件提取使用
  snprintf(g_installer_path, sizeof(g_installer_path), "%s", installer_path);
  snprintf(g_mc_version, sizeof(g_mc_version), "%s", mc_version);
  printf("  Downloading installer...\n");
  if (dl_one(installer_url, installer_path, "installer jar") != 0) {
    printf("ERROR: Failed to download installer jar.\n");
    free(chosen_forge_ver);
    goto done;
  }

  // 提取 install_profile.json
  printf("  Extracting install_profile.json...\n");
  char extract_cmd[1536];
  snprintf(extract_cmd, sizeof(extract_cmd),
           "unzip -p '%s' install_profile.json 2>/dev/null", installer_path);
  FILE *ep = popen(extract_cmd, "r");
  char *profile_raw = NULL;
  size_t pcap = 0, plen = 0;
  if (ep) {
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), ep)) {
      size_t cl = strlen(chunk);
      if (plen + cl + 1 > pcap) {
        pcap = pcap ? pcap * 2 : 16384;
        void *newbuf = realloc(profile_raw, pcap);
        if (!newbuf) {
          pclose(ep);
          free(profile_raw);
          printf("ERROR: Out of memory reading install_profile.json.\n");
          free(chosen_forge_ver);
          goto done;
        }
        profile_raw = newbuf;
      }
      memcpy(profile_raw + plen, chunk, cl);
      plen += cl;
      profile_raw[plen] = '\0';
    }
    pclose(ep);
  }

  if (!profile_raw) {
    printf("ERROR: Failed to extract install_profile.json.\n");
    remove(installer_path);
    free(chosen_forge_ver);
    goto done;
  }

  cJSON *install_profile = cJSON_Parse(profile_raw);
  free(profile_raw);
  if (!install_profile) {
    printf("ERROR: Failed to parse install_profile.json.\n");
    remove(installer_path);
    free(chosen_forge_ver);
    goto done;
  }

  cJSON *version_info = cJSON_GetObjectItem(install_profile, "versionInfo");
  cJSON *profile_libs = cJSON_GetObjectItem(install_profile, "libraries");
  cJSON *processors = cJSON_GetObjectItem(install_profile, "processors");
  cJSON *data = cJSON_GetObjectItem(install_profile, "data");
  int vi_owned = 0; // version_info 是否需要单独释放

  // 新格式（Forge 1.20+）：versionInfo 在 jar 内独立文件中，通过 "json"
  // 键指定路径
  if (!version_info) {
    cJSON *jpath = cJSON_GetObjectItem(install_profile, "json");
    if (jpath && cJSON_IsString(jpath)) {
      printf("  New installer format: extracting %s...\n", jpath->valuestring);
      // zip 内部路径不以 / 开头，去掉前导 /
      const char *inner_path = jpath->valuestring;
      while (*inner_path == '/')
        inner_path++;
      char vext[1536];
      snprintf(vext, sizeof(vext), "unzip -p '%s' '%s' 2>/dev/null",
               installer_path, inner_path);
      FILE *vp = popen(vext, "r");
      if (vp) {
        char *ver_raw = NULL;
        size_t vcap = 0, vlen = 0;
        char vchunk[4096];
        while (fgets(vchunk, sizeof(vchunk), vp)) {
          size_t vcl = strlen(vchunk);
          if (vlen + vcl + 1 > vcap) {
            vcap = vcap ? vcap * 2 : 16384;
            void *nv = realloc(ver_raw, vcap);
            if (!nv) {
              free(ver_raw);
              ver_raw = NULL;
              break;
            }
            ver_raw = nv;
          }
          memcpy(ver_raw + vlen, vchunk, vcl);
          vlen += vcl;
          ver_raw[vlen] = '\0';
        }
        pclose(vp);
        if (ver_raw) {
          version_info = cJSON_Parse(ver_raw);
          free(ver_raw);
          if (version_info)
            vi_owned = 1;
        }
      }
    }
    if (!version_info) {
      printf("ERROR: install_profile.json missing 'versionInfo'.\n");
      remove(installer_path);
      cJSON_Delete(install_profile);
      free(chosen_forge_ver);
      goto done;
    }
  }

  // ---- Phase 2: 下载原版 vanilla JSON ----
  printf("  Downloading vanilla JSON...\n");
  char vanilla_json_path[600];
  snprintf(vanilla_json_path, sizeof(vanilla_json_path),
           "/tmp/tmcl_vanilla_%s.json", mc_version);
  if (dl_one(vanilla_url, vanilla_json_path, "vanilla JSON") != 0) {
    printf("ERROR: Failed to download vanilla JSON.\n");
    goto forge_cancelled;
  }

  // 读取 vanilla JSON 到内存
  FILE *vf = fopen(vanilla_json_path, "r");
  cJSON *vanilla = NULL;
  if (vf) {
    fseek(vf, 0, SEEK_END);
    long vsz = ftell(vf);
    fseek(vf, 0, SEEK_SET);
    char *vd = malloc(vsz + 1);
    if (vd) {
      fread(vd, 1, vsz, vf);
      vd[vsz] = '\0';
      vanilla = cJSON_Parse(vd);
      free(vd);
    }
    fclose(vf);
  }
  if (!vanilla) {
    printf("ERROR: Failed to parse vanilla JSON.\n");
    goto forge_cancelled;
  }

  // ---- Phase 3: 收集所有下载任务 ----
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  int tcount = 0;
  // 8192 slots is far beyond practical max (~200 files), so no bounds check
  // needed

  // a) 原版 libraries（从 vanilla JSON）
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  int vlib_count = 0;
  if (v_libs && cJSON_IsArray(v_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, v_libs) {
      if (lib_rules_ok(cJSON_GetObjectItem(lib, "rules")))
        vlib_count++;
    }
    int cur = 0;
    cJSON_ArrayForEach(lib, v_libs) {
      if (!lib_rules_ok(cJSON_GetObjectItem(lib, "rules")))
        continue;
      cur++;
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[%d/%d] %s", cur, vlib_count, t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // b) 原版 client.jar
  cJSON *downloads = cJSON_GetObjectItem(vanilla, "downloads");
  if (downloads) {
    cJSON *client = cJSON_GetObjectItem(downloads, "client");
    if (client) {
      cJSON *jar_url = cJSON_GetObjectItem(client, "url");
      if (jar_url && cJSON_IsString(jar_url)) {
        snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                 jar_url->valuestring);
        snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s/%s.jar",
                 version_dir, custom_name);
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                 "vanilla client.jar");
        tcount++;
      }
    }
  }

  // c) install_profile 顶层 libraries（processor 依赖）
  if (profile_libs && cJSON_IsArray(profile_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, profile_libs) {
      // 强制设置 Maven URL 为 Forge/NeoForge 的仓库
      cJSON *existing_url = cJSON_GetObjectItem(lib, "url");
      if (!existing_url || !cJSON_IsString(existing_url) ||
          !existing_url->valuestring[0]) {
        char lib_url[512];
        snprintf(lib_url, sizeof(lib_url), "https://%s/", fcfg->maven_base);
        cJSON_AddStringToObject(lib, "url", lib_url);
      }
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[%s] %s", fcfg->artifact_id, t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // d) versionInfo libraries（运行时）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      cJSON *existing_url = cJSON_GetObjectItem(lib, "url");
      if (!existing_url || !cJSON_IsString(existing_url) ||
          !existing_url->valuestring[0]) {
        char lib_url[512];
        snprintf(lib_url, sizeof(lib_url), "https://%s/", fcfg->maven_base);
        cJSON_AddStringToObject(lib, "url", lib_url);
      }
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[%s] %s", fcfg->artifact_id, t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // e) asset index
  char ai_url[1024] = {0};
  char ai_path[512] = {0};
  cJSON *assetIndex = cJSON_GetObjectItem(vanilla, "assetIndex");
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

  // ---- Phase 4: 并行下载所有文件 ----
  printf("  Downloading %d files...\n\n", tcount);
  dl_parallel(tasks, tcount, 8);

  // 重试失败的文件
  char **failed_names = NULL;
  int failed_count = 0;
  if (g_cancel) {
    printf("\n  Cancelled by user, skipping retries.\n");
    goto forge_cancelled;
  }
  for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
    if (failed_names) {
      for (int k = 0; k < failed_count; k++)
        free(failed_names[k]);
      free(failed_names);
      failed_names = NULL;
      failed_count = 0;
    }
    int f = retry_failed(tasks, tcount, 8, &failed_names, &failed_count);
    if (f == 0)
      break;
  }

  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
    failed_names = NULL;
    failed_count = 0;
  }

  // ---- Phase 5: 顺序执行 processors ----
  if (!g_cancel && processors && cJSON_IsArray(processors)) {
    int ptotal = cJSON_GetArraySize(processors);
    printf("\n  Running %d processor(s)...\n\n", ptotal);
    cJSON *outputs = NULL;
    cJSON *proc;
    int pi = 0;
    cJSON_ArrayForEach(proc, processors) {
      if (g_cancel)
        break;
      pi++;
      printf("  [%d/%d]", pi, ptotal);
      if (run_forge_processor(proc, mcdir, version_dir, custom_name, data,
                              &outputs) != 0) {
        printf("  ERROR: Processor %d failed.\n", pi);
        if (outputs)
          cJSON_Delete(outputs);
        // 继续执行后续 processor（有些 processor 可选）
        outputs = NULL;
      }
    }
    if (outputs)
      cJSON_Delete(outputs);
  }

  // installer jar 现在可以安全清理（processor 需要的文件已提取到 /tmp）
  remove(installer_path);

  // ---- Phase 6: 生成自包含 version JSON ----
  if (!g_cancel) {
    printf("\n  Merging version JSON...\n");
    if (merge_forge_version_json(vanilla, version_info, custom_name, mc_version,
                                 modloader, chosen_forge_ver, json_path) != 0) {
      printf("ERROR: Failed to merge version JSON.\n");
    }
  }

  // ---- Phase 7: 下载 assets ----
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

      cJSON *ai_root = ai_data ? cJSON_Parse(ai_data) : NULL;
      if (ai_root) {
        cJSON *objects = cJSON_GetObjectItem(ai_root, "objects");
        if (objects && cJSON_IsObject(objects)) {
          int need_dl = 0, already = 0;
          cJSON *obj_item;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item))
              continue;
            const char *hash = hash_item->valuestring;
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%c%c/%s", mcdir,
                     hash[0], hash[1], hash);
            if (access(dest, F_OK) == 0)
              already++;
            else
              need_dl++;
          }

          DlTask *atasks = malloc((need_dl + 1) * sizeof(DlTask));
          int ac = 0;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item))
              continue;
            const char *hash = hash_item->valuestring;
            char first2[3] = {hash[0], hash[1], '\0'};
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%s/%s", mcdir,
                     first2, hash);
            if (access(dest, F_OK) == 0)
              continue;
            snprintf(atasks[ac].url, sizeof(atasks[ac].url),
                     "https://resources.download.minecraft.net/%s/%s", first2,
                     hash);
            // path may be up to 600 bytes; DlTask.dest is 512 — use strncpy
            // with explicit truncation guard
            strncpy(atasks[ac].dest, dest, sizeof(atasks[ac].dest) - 1);
            atasks[ac].dest[sizeof(atasks[ac].dest) - 1] = '\0';
            snprintf(atasks[ac].label, sizeof(atasks[ac].label), "[%d/%d] %s",
                     ac + 1, need_dl, obj_item->string);
            ac++;
          }
          if (ac > 0) {
            printf("\n  Assets: %d need download (%d already cached)\n\n", ac,
                   already);
            dl_parallel(atasks, ac, num_threads);
            for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
              char **a_fail = NULL;
              int a_fc = 0;
              int f = retry_failed(atasks, ac, num_threads, &a_fail, &a_fc);
              if (f == 0)
                break;
              if (a_fc > 0) {
                failed_names = realloc(failed_names,
                                       (failed_count + a_fc) * sizeof(char *));
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

forge_cancelled:
  // 清理
  free(tasks);
  cJSON_Delete(vanilla);
  if (vi_owned)
    cJSON_Delete(version_info);
  cJSON_Delete(install_profile);
  free(chosen_forge_ver);
  remove(vanilla_json_path);

  // 打印失败总结
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
  if (g_cancel)
    printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {
  }

done:
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}
