#include "new_mod.h"
#include "../config/config.h"
#include "mod.h"
#include "mod_version_page.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// 来自 version.h 的颜色映射
extern int modloader_color(const char *modloader, int selected);
void action_version(NewModState *state, cJSON **mod_search_result);
void action_search(NewModState *state, cJSON **mod_search_result);
void action_install(NewModState *state);
void action_modloader(NewModState *state, cJSON **mod_search_result);
void action_source(NewModState *state, cJSON **mod_search_result);

// ==== 读取整个文件内容到字符串（调用者需 free 返回的指针）====
char *read_file_content(const char *filepath) {
  FILE *fp = fopen(filepath, "rb");
  if (!fp)
    return NULL;

  // 获取文件大小
  fseek(fp, 0, SEEK_END);
  long fsize = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // 分配内存（+1 给 '\0'）
  char *content = (char *)malloc(fsize + 1);
  if (!content) {
    fclose(fp);
    return NULL;
  }

  // 读入全部数据
  fread(content, 1, fsize, fp);
  content[fsize] = '\0'; // 确保字符串结尾
  fclose(fp);

  return content;
}

cJSON *parse_json(char *file_path) {
  // 读取 JSON 文件
  char *json_str = read_file_content(file_path);
  if (!json_str) {
    free(json_str); // 原始字符串不再需要
    return NULL;
  }
  // 解析 JSON
  cJSON *root = cJSON_Parse(json_str);
  free(json_str); // 原始字符串不再需要
  if (!root) {
    cJSON_Delete(root);
    return NULL;
  }
  return root;
}

// ====== 初始化模组列表 =======
void new_mod_init(NewModState *state, ModState *ModState, char *mod_source) {
  memset(state, 0, sizeof(*state));
  snprintf(state->version_name, sizeof(state->version_name), "%s",
           ModState->version_name);
  snprintf(state->version_dir, sizeof(state->version_dir), "%s",
           ModState->version_dir);
  snprintf(state->modloader, sizeof(state->modloader), "%s",
           ModState->modloader);
  snprintf(state->mods_dir, sizeof(state->mods_dir), "%s", ModState->mods_dir);
  snprintf(state->mc_version, sizeof(state->mc_version), "%s",
           ModState->version);
  snprintf(state->loader, sizeof(state->loader), "%s", ModState->modloader);
  snprintf(state->order, sizeof(state->order), "%s", "hot");
  snprintf(state->source, sizeof(state->source), "%s", mod_source);
  state->action_count = 5;
}

// ---------- 底部操作栏 ----------
void rander_bottom(NewModState *state) {
  int row, col;
  getmaxyx(stdscr, row, col);

  mvprintw(row - 3, 0, "%-*s", col, " ");
  mvprintw(row - 2, 0, "%-*s", col, " ");
  mvprintw(row - 1, 0, "%-*s", col, " ");
  char *fmt[] = {"[i]nstall", "[v]ersion", "[m]odlader", "[s]ource",
                 "[/]search"};
  int cx = 0;
  for (int i = 0; i < state->action_count; i++) {
    if (i == state->action_sel)
      attron(A_REVERSE);
    mvprintw(row - 2, cx, "%s", fmt[i]);
    if (i == state->action_sel)
      attroff(A_REVERSE);
    cx += 11;
  }

  mvprintw(row - 1, 0, "& detail");
  mvprintw(row - 1, 11, "%s", state->mc_version);
  attron(COLOR_PAIR(modloader_color(state->loader, 0)));
  mvprintw(row - 1, 22, "%s", state->loader);
  attroff(COLOR_PAIR(modloader_color(state->loader, 0)));
  mvprintw(row - 1, 33, "%s", state->source);
  mvprintw(row - 1, 44, "%s", state->search);
}

// ========== 渲染打印 ==========
void rander_new_mod_page(NewModState *state) {
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);

  mvprintw(0, 0, "Install mod for: ");
  attron(A_BOLD);
  printw("%s", state->version_name);
  attroff(A_BOLD);
  printw(" (");
  int mc = modloader_color(state->modloader, 0);
  if (mc)
    attron(COLOR_PAIR(mc));
  printw("%s", state->modloader);
  if (mc)
    attroff(COLOR_PAIR(mc));
  printw(")");

  // ---------- 中间：统一渲染 ModResult 数组 ----------
  ModResult *results = state->search_results;
  if (results && state->count > 0) {
    for (int i = 0; i < state->count && 2 * i <= row; i++) {
      int idx = i + state->scroll_offset;
      if (idx >= state->count)
        break;
      ModResult *r = &results[idx];

      mvprintw(2 + 2 * i, 0, "%-*s", col, " ");
      if (idx == state->selected) {
        attron(A_REVERSE);
        snprintf(state->mod_id, sizeof(state->mod_id), "%s", r->id);
        snprintf(state->mod_title, sizeof(state->mod_title), "%s", r->title);
      }
      attron(A_BOLD);
      attron(COLOR_PAIR(modloader_color("fabric", state->selected == idx)));
      mvprintw(2 + 2 * i, 0, "%2d %-*s", idx, col - 3, r->title);
      attroff(A_BOLD);
      attroff(COLOR_PAIR(modloader_color("fabric", state->selected == idx)));
      mvprintw(3 + 2 * i, 0, "   %-*s", col - 3, r->description);
      if (idx == state->selected)
        attroff(A_REVERSE);
    }
  }

  rander_bottom(state);
}

// ============ 主函数 ============
void new_mod_page(NewModState *state, ConfigState *ConfigState) {
  (void)ConfigState;
  // 尝试加载已有缓存
  char *cache_file;
  int src;
  if (strcmp(state->source, "Modrinth") == 0) {
    cache_file = "/tmp/tmcl_mr_mod_search_result.json";
    src = SOURCE_MODRINTH;
  } else {
    cache_file = "/tmp/tmcl_cf_mod_search_result.json";
    src = SOURCE_CURSEFORGE;
  }
  cJSON *cached = parse_json(cache_file);
  if (cached) {
    state->count = mod_api_parse_search(cached, src, &state->search_results);
    cJSON_Delete(cached);
  }

  while (1) {
    int row, col;
    getmaxyx(stdscr, row, col);
    rander_new_mod_page(state);
    int c = getch();

    // 列表导航: j/k/↓/↑
    if ((c == 'j' || c == KEY_DOWN) && state->count > 0 &&
        state->selected < state->count - 1)
      state->selected++;
    if ((c == 'k' || c == KEY_UP) && state->selected > 0)
      state->selected--;

    // offset
    if (2 * state->selected > row - 7 + 2 * state->scroll_offset)
      state->scroll_offset++;
    if (state->selected < state->scroll_offset)
      state->scroll_offset--;

    // 操作栏导航: h/←, l/→
    if (c == 'h' || c == KEY_LEFT)
      state->action_sel =
          (state->action_sel + state->action_count - 1) % state->action_count;
    if (c == 'l' || c == KEY_RIGHT)
      state->action_sel = (state->action_sel + 1) % state->action_count;

    int action = -1;
    switch (c) {
    case 'i':
      state->action_sel = 0;
      action = 0;
      break;
    case 'v':
      state->action_sel = 1;
      action = 1;
      break;
    case 'm':
      state->action_sel = 2;
      action = 2;
      break;
    case 's':
      state->action_sel = 3;
      action = 3;
      break;
    case '/':
      state->action_sel = 4;
      action = 4;
      break;
    case '\n':
      action = state->action_sel;
      break;
    case 'q':
      clear();
      return;
      break;
    }

    cJSON *dummy = NULL; // 不再使用 cJSON 直接渲染
    switch (action) {
    case 0:
      rander_new_mod_page(state);
      action_install(state);
      break;
    case 1:
      action_version(state, &dummy);
      break;
    case 2:
      action_modloader(state, &dummy);
      break;
    case 3:
      action_source(state, &dummy);
      break;
    case 4:
      action_search(state, &dummy);
      break;
    }
  }
  clear();
}

// ---------- 搜索之 download json（Modrinth + CurseForge）----------
bool mod_search_dld_json(NewModState *state) {
  int row, col;
  getmaxyx(stdscr, row, col);
  mvprintw(row - 3, 0, "loading ......");
  refresh();

  char cmd[2048];
  if (strcmp(state->source, "Modrinth") == 0) {
    // 只有 mc_version 非空才加 versions facet
    char mr_facets[512];
    if (state->mc_version[0])
      snprintf(mr_facets, sizeof(mr_facets), "[[\"versions:%s\"],[\"categories:%s\"]]",
               state->mc_version, state->modloader);
    else
      snprintf(mr_facets, sizeof(mr_facets), "[[\"categories:%s\"]]",
               state->modloader);

    snprintf(
        cmd, sizeof(cmd),
        "curl -s -m 20 -o /tmp/tmcl_mr_mod_search_result.json "
        "-G "
        "--data-urlencode 'query=%s' "
        "--data-urlencode 'project_type=mod' "
        "--data-urlencode 'limit=100' "
        "--data-urlencode 'facets=%s' "
        "https://api.modrinth.com/v2/search",
        state->search, mr_facets);
  } else if (strcmp(state->source, "CurseForge") == 0) {
    // 构建 modLoaderType 参数
    char loader_param[32] = "";
    int lt = curseforge_loader_id(state->loader);
    if (lt > 0)
      snprintf(loader_param, sizeof(loader_param), "&modLoaderType=%d", lt);

    // 只有 mc_version 非空才加 gameVersion 过滤
    // 13 字节前缀 + 最多 63 字节版本号 → 96 与 mod_version_page.c 一致
    char gameVer_param[96] = "";
    if (state->mc_version[0])
      snprintf(gameVer_param, sizeof(gameVer_param), "&gameVersion=%s",
               state->mc_version);

    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_cf_mod_search_result.json "
             "-H 'x-api-key: %s' "
             "\"https://api.curseforge.com/v1/mods/search?"
             "gameId=432&classId=6&sortField=2&sortOrder=desc"
             "&searchFilter=%s"
             "%s"
             "%s\"",
             CURSEFORGE_API_KEY,
             state->search[0] ? state->search : "",
             gameVer_param,
             loader_param);
  }

  if (system(cmd) != 0) {
    clear();
    mvprintw(3, 2, "FIELD to get json !!!");
    mvprintw(4, 2, "You may check your network");
    mvprintw(5, 2, "Press any key to continue");
    getch();
    return 1;
  }
  return 0;
}

// ---------- 安装和详情 ----------
void action_install(NewModState *state) {
  if (state->count <= 0)
    return;

  int row, col;
  getmaxyx(stdscr, row, col);
  mvprintw(row - 3, 0, "loading ......");
  refresh();

  // ----- 统一下载详情 JSON -----
  char cmd[2048];
  char *get_tmp;
  if (strcmp(state->source, "Modrinth") == 0) {
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_mod_get_result.json "
             "https://api.modrinth.com/v2/project/%s",
             state->mod_id);
    get_tmp = "/tmp/tmcl_mod_get_result.json";
  } else { // CurseForge
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_cf_mod_get_result.json "
             "-H 'x-api-key: %s' "
             "\"https://api.curseforge.com/v1/mods/%s\"",
             CURSEFORGE_API_KEY, state->mod_id);
    get_tmp = "/tmp/tmcl_cf_mod_get_result.json";
  }

  if (system(cmd) != 0) {
    clear();
    mvprintw(3, 2, "FIELD !!!");
    mvprintw(4, 2, "Press any key to continue");
    getch();
    return;
  }

  // ----- 统一解析 -----
  cJSON *get_root = parse_json(get_tmp);
  if (!get_root)
    return;

  int src = (strcmp(state->source, "Modrinth") == 0) ? SOURCE_MODRINTH
                                                      : SOURCE_CURSEFORGE;
  mod_api_parse_detail(get_root, src, &state->detail);
  cJSON_Delete(get_root);

  // CurseForge: /v1/mods/{id} 不返回 description，需要单独获取
  if (src == SOURCE_CURSEFORGE) {
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_cf_mod_desc.json "
             "-H 'x-api-key: %s' "
             "\"https://api.curseforge.com/v1/mods/%s/description\"",
             CURSEFORGE_API_KEY, state->mod_id);
    if (system(cmd) == 0) {
      cJSON *desc_root = parse_json("/tmp/tmcl_cf_mod_desc.json");
      if (desc_root) {
        cJSON *desc_data = cJSON_GetObjectItem(desc_root, "data");
        if (cJSON_IsString(desc_data)) {
          size_t len = strlen(desc_data->valuestring);
          if (len >= sizeof(state->detail.body))
            len = sizeof(state->detail.body) - 1;
          memcpy(state->detail.body, desc_data->valuestring, len);
          state->detail.body[len] = '\0';
        }
        cJSON_Delete(desc_root);
      }
    }

    // 从 files 端点提取 mod loader（latestFiles[0].gameVersions 不总是包含 loader）
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_cf_mod_files.json "
             "-H 'x-api-key: %s' "
             "\"https://api.curseforge.com/v1/mods/%s/files?pageSize=10\"",
             CURSEFORGE_API_KEY, state->mod_id);
    if (system(cmd) == 0) {
      cJSON *files_root = parse_json("/tmp/tmcl_cf_mod_files.json");
      if (files_root) {
        cJSON *files_data = cJSON_GetObjectItem(files_root, "data");
        if (cJSON_IsArray(files_data)) {
          int fn = cJSON_GetArraySize(files_data);
          char seen[256] = ""; // 去重
          int pos = 0;
          for (int fi = 0; fi < fn; fi++) {
            cJSON *file = cJSON_GetArrayItem(files_data, fi);
            cJSON *gv_array = cJSON_GetObjectItem(file, "gameVersions");
            if (!cJSON_IsArray(gv_array))
              continue;
            int gn = cJSON_GetArraySize(gv_array);
            for (int gi = 0; gi < gn; gi++) {
              cJSON *gv = cJSON_GetArrayItem(gv_array, gi);
              if (!cJSON_IsString(gv))
                continue;
              const char *v = gv->valuestring;
              const char *lc = NULL;
              if (strcmp(v, "Forge") == 0) lc = "forge";
              else if (strcmp(v, "Fabric") == 0) lc = "fabric";
              else if (strcmp(v, "NeoForge") == 0) lc = "neoforge";
              else if (strcmp(v, "Quilt") == 0) lc = "quilt";
              if (!lc) continue;
              // 去重
              if (strstr(seen, lc) == NULL) {
                if (pos > 0) pos += snprintf(seen + pos, sizeof(seen) - pos, ",");
                pos += snprintf(seen + pos, sizeof(seen) - pos, "%s", lc);
                if ((size_t)pos >= sizeof(seen)) break;
              }
            }
          }
          snprintf(state->detail.loaders_list, sizeof(state->detail.loaders_list), "%s", seen);
        }
        cJSON_Delete(files_root);
      }
    }
  }

  // ----- 渲染 -----
  clear();
  getmaxyx(stdscr, row, col);
  ModDetail *d = &state->detail;

  move(0, 0);
  printw("\n  >> %s <<\n\n", d->title);
  printw("dls: %-13d", d->downloads);
  printw("fls: %-13d", d->followers);
  printw("clt: %-13s", d->client_side);
  printw("svr: %-13s", d->server_side);
  printw("\nMod Loader:       ");

  // 从 loaders_list 解析并着色
  char list_copy[256];
  snprintf(list_copy, sizeof(list_copy), "%s", d->loaders_list);
  char *token = strtok(list_copy, ",");
  while (token) {
    while (*token == ' ')
      token++;
    int loader_color = modloader_color(token, 0);
    attron(COLOR_PAIR(loader_color));
    printw("%-18s", token);
    attroff(COLOR_PAIR(loader_color));
    token = strtok(NULL, ",");
  }

  printw("\n\n==> DESCRIPTION:\n%s\n\n", d->description);
  printw("==> DETAIL:\n%s", d->body);
  mvprintw(row - 1, 0, "%-*s", col, " ");
  mvprintw(row - 2, 0, "%-*s", col, " ");
  mvprintw(row - 1, 0, "Do you want to [i]nstall this mod? [Y/n]");
  int ch = getch();
  if (ch == 'y' || ch == 'Y' || ch == '\n' || ch == 'i') {
    ModVersionState ModVersionState = {0};
    mod_version_page(&ModVersionState, state);
  }
}

// ---------- 搜索 ----------
void action_search(NewModState *state, cJSON **mod_search_result) {
  (void)mod_search_result;
  int row, col;
  getmaxyx(stdscr, row, col);
  rander_new_mod_page(state);
  mvprintw(row - 1, 44, "%-*s", col - 44, " ");
  move(row - 1, 44);
  curs_set(2);
  echo();
  char input[1024];
  getnstr(input, sizeof(input) - 1);
  curs_set(0);
  noecho();
  snprintf(state->search, sizeof(state->search), "%s", input);

  // 统一下载搜索 JSON
  if (mod_search_dld_json(state) != 0)
    return;

  // 统一解析
  char *tmp_file;
  int src;
  if (strcmp(state->source, "Modrinth") == 0) {
    tmp_file = "/tmp/tmcl_mr_mod_search_result.json";
    src = SOURCE_MODRINTH;
  } else {
    tmp_file = "/tmp/tmcl_cf_mod_search_result.json";
    src = SOURCE_CURSEFORGE;
  }

  cJSON *root = parse_json(tmp_file);
  if (!root)
    return;

  free(state->search_results);
  state->count = mod_api_parse_search(root, src, &state->search_results);
  cJSON_Delete(root);

  state->selected = 0;
  state->scroll_offset = 0;
}

// type mc version to fliter
void action_version(NewModState *state, cJSON **mod_search_result) {
  (void)mod_search_result;
  int row, col;
  getmaxyx(stdscr, row, col);
  rander_new_mod_page(state);
  mvprintw(row - 1, 11, "%-*s", col - 11, " ");
  mvprintw(row - 2, 11, "%-*s", col - 11, " ");
  mvprintw(row - 2, 11, "MC version to fliter:  Type nothing to return");
  mvprintw(row - 1, 33, "Type space to disable flitering");
  move(row - 1, 11);
  curs_set(2);
  echo();
  char input[64] = {0};
  getnstr(input, sizeof(input) - 1);
  curs_set(0);
  noecho();
  // 过滤词上限保持 32 字符（MC 版本号远短于此），buffer 已扩为 64 仅为对齐上游
  if (input[32] || !input[0]) {
    return;
  }
  if (input[0] == ' ') {
    // 空格 → 禁用 MC 版本过滤
    snprintf(state->mc_version, sizeof(state->mc_version), "%s", "");
  } else {
    for (int i = 0; i < (int)sizeof(input); i++) {
      if (input[i + 1] == ' ' || input[i] == '\t') {
        return;
      }
    }
    snprintf(state->mc_version, sizeof(state->mc_version), "%s", input);
  }

  // 统一下载 + 解析
  free(state->search_results);
  state->search_results = NULL;
  if (!mod_search_dld_json(state)) {
    char *tmp_file;
    int src;
    if (strcmp(state->source, "Modrinth") == 0) {
      tmp_file = "/tmp/tmcl_mr_mod_search_result.json";
      src = SOURCE_MODRINTH;
    } else {
      tmp_file = "/tmp/tmcl_cf_mod_search_result.json";
      src = SOURCE_CURSEFORGE;
    }
    cJSON *root = parse_json(tmp_file);
    if (root) {
      state->count = mod_api_parse_search(root, src, &state->search_results);
      cJSON_Delete(root);
    }
  }

  state->selected = 0;
  state->scroll_offset = 0;
}

// chose modloader
void action_modloader(NewModState *state, cJSON **mod_search_result) {
  (void)mod_search_result;
  int row, col;
  getmaxyx(stdscr, row, col);

  int ch;
  char *mod_loaders[] = {"fabric", "forge", "neoforge", "quilt", "disable"};
  int mod_loaders_count = 5; // 加上disable
  int mod_loader_selected = 0;
  for (int i = 0; i < mod_loaders_count; i++) {
    if (strcmp(state->loader, mod_loaders[i]) == 0) {
      mod_loader_selected = i;
      break;
    }
  }

  mvprintw(row - mod_loaders_count - 1, 21, "______________");
  for (int i = 0; i <= mod_loaders_count; i++) {
    mvprintw(row - mod_loaders_count + i, 21, "|");
    mvprintw(row - mod_loaders_count + i, 34, "|");
  }

  while (1) {
    for (int i = 0; i < mod_loaders_count; i++) {
      attron(COLOR_PAIR(
          modloader_color(mod_loaders[i], mod_loader_selected == i ? 1 : 0)));
      if (mod_loader_selected == i)
        attron(A_REVERSE);
      mvprintw(row - mod_loaders_count + i, 22, "%-12s", mod_loaders[i]);
      attroff(COLOR_PAIR(
          modloader_color(mod_loaders[i], mod_loader_selected == i ? 1 : 0)));
      if (mod_loader_selected == i)
        attroff(A_REVERSE);
    }

    ch = getch();
    switch (ch) {
    case 'j':
      mod_loader_selected++;
      break;
    case 'k':
      mod_loader_selected--;
      break;
    case '\n':
      snprintf(state->loader, sizeof(state->loader), "%s",
               mod_loaders[mod_loader_selected]);
      if (strcmp(state->loader, "disable") == 0)
        snprintf(state->loader, sizeof(state->loader), "");

      // 统一下载 + 解析（基于 source，非 loader）
      free(state->search_results);
      state->search_results = NULL;
      if (!mod_search_dld_json(state)) {
        char *tmp_file;
        int src;
        if (strcmp(state->source, "Modrinth") == 0) {
          tmp_file = "/tmp/tmcl_mr_mod_search_result.json";
          src = SOURCE_MODRINTH;
        } else {
          tmp_file = "/tmp/tmcl_cf_mod_search_result.json";
          src = SOURCE_CURSEFORGE;
        }
        cJSON *root = parse_json(tmp_file);
        if (root) {
          state->count = mod_api_parse_search(root, src, &state->search_results);
          cJSON_Delete(root);
        }
      }

      state->selected = 0;
      state->scroll_offset = 0;

    case 'q':
      return;
    }

    if (mod_loader_selected < 0)
      mod_loader_selected = 0;
    if (mod_loader_selected >= mod_loaders_count)
      mod_loader_selected = mod_loaders_count - 1;
  }
}

// 改变mod download source
void action_source(NewModState *state, cJSON **mod_search_result) {
  (void)mod_search_result;
  // 先清除旧结果
  free(state->search_results);
  state->search_results = NULL;
  state->count = 0;

  // MR → CF
  if (strcmp(state->source, "Modrinth") == 0) {
    snprintf(state->source, sizeof(state->source), "CurseForge");
    // 用 CurseForge 源重新搜索
    if (state->search[0] != '\0' && mod_search_dld_json(state) == 0) {
      cJSON *root = parse_json("/tmp/tmcl_cf_mod_search_result.json");
      if (root) {
        state->count = mod_api_parse_search(root, SOURCE_CURSEFORGE,
                                            &state->search_results);
        cJSON_Delete(root);
      }
    }
  }
  // CF → MR
  else if (strcmp(state->source, "CurseForge") == 0) {
    snprintf(state->source, sizeof(state->source), "Modrinth");
    if (state->search[0] != '\0' && mod_search_dld_json(state) == 0) {
      cJSON *root = parse_json("/tmp/tmcl_mr_mod_search_result.json");
      if (root) {
        state->count = mod_api_parse_search(root, SOURCE_MODRINTH,
                                            &state->search_results);
        cJSON_Delete(root);
      }
    }
  }

  state->selected = 0;
  state->scroll_offset = 0;
}

// ---- 清理 ----
void new_mod_cleanup(NewModState *state) {
  free(state->search_results);
  state->search_results = NULL;
  state->count = 0;
}
