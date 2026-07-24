#include "new_mod.h"
#include "../config/config.h"
#include "mod.h"
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
extern void action_install(NewModState *state);
void action_search(NewModState *state, cJSON **mod_search_result);

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
  snprintf(state->version, sizeof(state->version), "%s", ModState->version);
  snprintf(state->loader, sizeof(state->loader), "%s", ModState->modloader);
  snprintf(state->order, sizeof(state->order), "%s", "hot");
  snprintf(state->source, sizeof(state->source), "%s", mod_source);
  state->action_count = 5;
}

// ========== 渲染打印 ==========
void rander_new_mod_page(NewModState *state, cJSON *root) {
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);

  mvprintw(0, 0, "Install mod for: ");
  attron(A_BOLD);
  printw("%s", state->version_name);
  attroff(A_BOLD);

  // modloader 带色
  printw(" (");
  int mc = modloader_color(state->modloader, 0);
  if (mc)
    attron(COLOR_PAIR(mc));
  printw("%s", state->modloader);
  if (mc)
    attroff(COLOR_PAIR(mc));
  printw(")");

  // ---------- 中间 ----------
  if (!root) {
    goto bottom;
  }

  // ----- modrinth -----
  if (strcmp(state->source, "Modrinth") == 0) {
    cJSON *hits = cJSON_GetObjectItem(root, "hits");
    if (cJSON_IsArray(hits)) {
      int count = cJSON_GetArraySize(hits);
      state->count = count;

      for (int i = 0; i < count && 2 * i <= row; i++) {
        cJSON *item = cJSON_GetArrayItem(hits, i + state->scroll_offset);
        cJSON *title = cJSON_GetObjectItem(item, "title");
        cJSON *description = cJSON_GetObjectItem(item, "description");
        if (cJSON_IsString(title)) {
          mvprintw(2 + 2 * i, 0, "%-*s", col, " ");
          if (i + state->scroll_offset == state->selected) {
            attron(A_REVERSE);
            cJSON *mod_id = cJSON_GetObjectItem(item, "project_id");
            snprintf(state->mod_id, sizeof(state->mod_id), "%s",
                     mod_id->valuestring);
          }
          attron(A_BOLD);
          mvprintw(2 + 2 * i, 0, "%2d %-*s", i + state->scroll_offset, col - 3,
                   title->valuestring);
          attroff(A_BOLD);
          mvprintw(3 + 2 * i, 0, "   %-*s", col - 3, description->valuestring);
          if (i + state->scroll_offset == state->selected)
            attroff(A_REVERSE);
        }
      }
    }
  }

  // ----- curseforge -----
  if (strcmp(state->loader, "CurseForge")) {
  }

  // cJSON_Delete(root);

  // ---------- 底部操作栏 ----------
bottom:
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
  mvprintw(row - 1, 11, "%s", state->version);
  mvprintw(row - 1, 22, "%s", state->loader);
  mvprintw(row - 1, 33, "%s", state->source);
  mvprintw(row - 1, 44, "%s", state->search);
}

// ============ 主函数 ============
void new_mod_page(NewModState *state, ConfigState *ConfigState) {
  cJSON *mod_search_result = parse_json("/tmp/mod_search_result.json");
  while (1) {
    int row, col;
    getmaxyx(stdscr, row, col);
    rander_new_mod_page(state, mod_search_result);
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

    switch (action) {
    case 0:
      action_install(state);
      break;
    case 1:
      break;
    case 2:
      break;
    case 3:
      break;
    case 4:
      action_search(state, &mod_search_result);
      break;
    }
  }
  clear();
}

// ---------- 搜索，下载json文件 ----------
void mr_mod_search_json_dld(NewModState *state) {
  if (strcmp(state->source, "Modrinth") == 0) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/mod_search_result.json "
             "-G --data-urlencode 'query=%s' "
             "--data-urlencode 'project_type=mod' "
             "--data-urlencode 'limit=100' "
             "--data-urlencode 'versions=%s' "
             "--data-urlencode 'categories=%s' "
             "https://api.modrinth.com/v2/search",
             state->search, state->version, state->loader);
    if (system(cmd) != 0) {
      clear();
      mvprintw(3, 2, "FIELD !!!");
      mvprintw(4, 2, "Press any key to continue");
      getch();
    }
  }
}

// ---------- 搜索 ----------
void action_search(NewModState *state, cJSON **mod_search_result) {
  int row, col;
  getmaxyx(stdscr, row, col);
  rander_new_mod_page(state, *mod_search_result);
  mvprintw(row - 1, 44, "%-*s", col - 44, " ");
  move(row - 1, 44);
  curs_set(2);
  echo();
  char input[1024];
  getnstr(input, sizeof(input) - 1);
  curs_set(0);
  noecho();

  mvprintw(1, 0, "loading ......");
  refresh();

  snprintf(state->search, sizeof(state->search), "%s", input);
  mr_mod_search_json_dld(state);
  *mod_search_result = parse_json("/tmp/mod_search_result.json");

  state->selected = 0;
}
