#include "mod_version_page.h"
#include "new_mod.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

extern cJSON *parse_json(char *filepath);
extern int modloader_color(const char *modloader, int selected);

void fliter_mod_version(ModVersionState *state, NewModState *NewModState) {
  // ---------- Modrinth ----------
  if (strcmp(NewModState->source, "Modrinth") == 0) {
    int array_size = cJSON_GetArraySize(state->mod_version_root);
    state->count_all = array_size;
    state->count_flitered = 0;
    for (int i = 0; i < array_size; i++) {
      cJSON *version = cJSON_GetArrayItem(state->mod_version_root, i);
      cJSON *game_versions = cJSON_GetObjectItem(version, "game_versions");
      cJSON *loaders = cJSON_GetObjectItem(version, "loaders");

      // 过滤条件检查（同前）
      int mc_ok = 0;
      if (cJSON_IsArray(game_versions)) {
        int gv_size = cJSON_GetArraySize(game_versions);
        for (int j = 0; j < gv_size; j++) {
          cJSON *gv = cJSON_GetArrayItem(game_versions, j);
          if (strcmp(state->mc_version, gv->valuestring) == 0) {
            mc_ok = 1;
            break;
          }
        }
      }
      if (!mc_ok)
        continue;

      int loader_ok = 0;
      if (cJSON_IsArray(loaders)) {
        int l_size = cJSON_GetArraySize(loaders);
        for (int j = 0; j < l_size; j++) {
          cJSON *loader = cJSON_GetArrayItem(loaders, j);
          if (strcmp(state->modloader, loader->valuestring) == 0) {
            loader_ok = 1;
            break;
          }
        }
      }
      if (!loader_ok)
        continue;

      // 通过过滤，存储指针
      state->filtered_versions[state->count_flitered] = version;
      state->count_flitered++;
    }
  }

  // ---------- CurseForge ----------
  else if (strcmp(NewModState->source, "CurseForge")) {
  }
}

bool mod_version_init(ModVersionState *state, NewModState *NewModState) {
  int row, col;
  getmaxyx(stdscr, row, col);
  state->action_count = 4;
  state->selected = 0;
  state->scroll_offset = 0;

  snprintf(state->mods_dir, sizeof(state->mods_dir), "%s",
           NewModState->mods_dir);
  snprintf(state->modloader, sizeof(state->modloader), "%s",
           NewModState->modloader);
  snprintf(state->mc_version, sizeof(state->mc_version), "%s",
           NewModState->mc_version);

  // ---------- modrinth ----------
  if (strcmp(NewModState->source, "Modrinth") == 0) {
    mvprintw(row - 2, 0, "loading ......");
    refresh();
    // ----- 下载json文件 -----
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/mod_version.json "
             "https://api.modrinth.com/v2/project/%s/version",
             NewModState->mod_id);
    if (system(cmd) != 0) {
      clear();
      printw("FIELD to download !!!");
      printw("Press any key to continue");
      getch();
      return 1;
    }
    // ----- 打开并解析json文件 -----
    state->mod_version_root = parse_json("/tmp/mod_version.json");
    if (!state->mod_version_root) {
      clear();
      printw("FIELD to parse json file !!!");
      printw("Press any key to continue");
      getch();
      return 1;
    }

    cJSON *version = cJSON_GetArrayItem(state->mod_version_root, 0);
    if (!version) {
      clear();
      printw("There is nothing to download !!!");
      printw("Press any key to continue");
      getch();
      return 1;
    }
    int arry_size = cJSON_GetArraySize(state->mod_version_root);
    state->count_all = arry_size;
    cJSON *files = cJSON_GetObjectItem(version, "files");
    cJSON *file0 = cJSON_GetArrayItem(files, 0);
    cJSON *filename = cJSON_GetObjectItem(file0, "filename");
    state->filtered_versions = malloc(state->count_all * sizeof(cJSON *));

    // ---------- CurseForge ----------
  } else if (strcmp(NewModState->source, "CurseForge") == 0) {
  }

  return 0;
}

// =============== 渲染打印 ===============
void rander_mod_version(ModVersionState *state, NewModState *NewModState) {
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);

  // ---------- top bar ----------
  mvprintw(0, 0, "Install ");
  attron(A_BOLD);
  printw("%s", NewModState->mod_title);
  attroff(A_BOLD);
  printw(" for ");
  attron(A_BOLD);
  printw("%s", NewModState->version_name);
  attroff(A_BOLD);
  // modloader 带色
  printw(" (");
  int mc = modloader_color(NewModState->modloader, 0);
  if (mc)
    attron(COLOR_PAIR(mc));
  printw("%s", NewModState->modloader);
  if (mc)
    attroff(COLOR_PAIR(mc));
  printw(")");

  // =-=-=-=-=-=-=- 中间 -=--=-=-=-=-=-=
  // ------------ modrinth -------------
  if (strcmp(NewModState->source, "Modrinth") == 0) {
    for (int i = 0; i < state->count_flitered && 2 * i <= row; i++) {
      cJSON *version = state->filtered_versions[i];
      bool selected = 0;
      if (i + state->scroll_offset == state->selected) {
        attron(A_REVERSE);
        selected = 1;
      }
      mvprintw(2 + 2 * i, 0, "%-*s", col, " ");
      mvprintw(3 + 2 * i, 0, "%-*s", col, " ");
      cJSON *version_number = cJSON_GetObjectItem(version, "version_number");
      cJSON *version_type = cJSON_GetObjectItem(version, "version_type");
      cJSON *downloads = cJSON_GetObjectItem(version, "downloads");
      attron(A_BOLD);
      mvprintw(2 + 2 * i, 0, " %2d ", i + state->scroll_offset);
      if (strcmp(version_type->valuestring, "release") == 0) {
        attron(COLOR_PAIR(modloader_color("fabric", selected)));
        printw("R");
        attroff(COLOR_PAIR(modloader_color("fabric", selected)));
      } else if (strcmp(version_type->valuestring, "beta") == 0) {
        attron(COLOR_PAIR(modloader_color("fabric", selected)));
        printw("B");
        attroff(COLOR_PAIR(modloader_color("fabric", selected)));
      } else if (strcmp(version_type->valuestring, "alpha") == 0) {
        attron(COLOR_PAIR(modloader_color("fabric", selected)));
        printw("A");
        attroff(COLOR_PAIR(modloader_color("fabric", selected)));
      } else {
        printw("U"); // Unkown
      }
      printw(" %s", version_number->valuestring);
      attroff(A_BOLD);
      printw("  D:%d", downloads->valueint);

      // 第二行
      move(3 + 2 * i, 4);
      cJSON *game_versions = cJSON_GetObjectItem(version, "game_versions");
      cJSON *loaders = cJSON_GetObjectItem(version, "loaders");
      int game_versions_count = cJSON_GetArraySize(game_versions);
      int loaders_count = cJSON_GetArraySize(loaders);
      for (int i = 0; i < game_versions_count; i++) {
        printw("%s ", cJSON_GetArrayItem(game_versions, i)->valuestring);
      }
      for (int i = 0; i < loaders_count; i++) {
        printw(" %s", cJSON_GetArrayItem(loaders, i)->valuestring);
      }

      // end
      if (i + state->scroll_offset == state->selected)
        attroff(A_REVERSE);
    }
  }

  // ---------- bottom bar ----------
  mvprintw(row - 3, 0, "%-*s", col, " ");
  mvprintw(row - 2, 0, "%-*s", col, " ");
  mvprintw(row - 1, 0, "%-*s", col, " ");
  char *fmt[] = {"[i]nstall", "[v]ersion", "[m]odlader", "[a]s"};
  int cx = 0;
  for (int i = 0; i < state->action_count; i++) {
    if (i == state->action_sel)
      attron(A_REVERSE);
    mvprintw(row - 2, cx, "%s", fmt[i]);
    if (i == state->action_sel)
      attroff(A_REVERSE);
    cx += 11;
  }

  mvprintw(row - 1, 0, "&download");
  mvprintw(row - 1, 11, "%s", state->mc_version);
  attron(COLOR_PAIR(modloader_color(state->modloader, 0)));
  mvprintw(row - 1, 22, "%s", state->modloader);
  attroff(COLOR_PAIR(modloader_color(state->modloader, 0)));
  mvprintw(row - 1, 33, "%s", state->install_as);
}

void aaction_install(ModVersionState *state) {}

// ========== 主函数 ==========
void mod_version_page(ModVersionState *state, NewModState *NewModState) {
  if (mod_version_init(state, NewModState))
    return;
  int pre_selected = -1;
  fliter_mod_version(state, NewModState);
  while (1) {
    int row, col;
    getmaxyx(stdscr, row, col);

    if (state->selected != pre_selected) {
      cJSON *version = state->filtered_versions[state->selected];
      cJSON *files = cJSON_GetObjectItem(version, "files");
      cJSON *file0 = cJSON_GetArrayItem(files, 0);
      cJSON *filename = cJSON_GetObjectItem(file0, "filename");
      snprintf(state->install_as, sizeof(state->install_as), "%s/%s",
               state->mods_dir, filename->valuestring);
      pre_selected = state->selected;
    }

    rander_mod_version(state, NewModState);
    int c = getch();

    // 列表导航: j/k/↓/↑
    if ((c == 'j' || c == KEY_DOWN) && state->count_flitered > 0 &&
        state->selected < state->count_flitered - 1) {
      state->selected++;
    }
    if ((c == 'k' || c == KEY_UP) && state->selected > 0) {
      state->selected--;
    }

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
    case 'a':
      state->action_sel = 3;
      action = 3;
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
      aaction_install(state);
      break;
    case 1:
      break;
    case 2:
      break;
    case 3:
      break;
    }
  }
  clear();
}
