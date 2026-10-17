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

// ========== 初始化（下载 + 解析 + 过滤）==========
bool mod_version_init(ModVersionState *state, NewModState *NewModState) {
  int row, col;
  getmaxyx(stdscr, row, col);
  (void)col;
  state->action_count = 4;
  state->selected = 0;
  state->scroll_offset = 0;

  snprintf(state->mods_dir, sizeof(state->mods_dir), "%s",
           NewModState->mods_dir);
  snprintf(state->modloader, sizeof(state->modloader), "%s",
           NewModState->modloader);
  snprintf(state->mc_version, sizeof(state->mc_version), "%s",
           NewModState->mc_version);
  snprintf(state->install_as, sizeof(state->install_as), "%s", "");

  mvprintw(row - 2, 0, "loading ......");
  refresh();

  // ----- 下载 JSON -----
  char cmd[2048];
  char *tmp_file;
  if (strcmp(NewModState->source, "Modrinth") == 0) {
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_mod_version.json "
             "https://api.modrinth.com/v2/project/%s/version",
             NewModState->mod_id);
    tmp_file = "/tmp/tmcl_mod_version.json";
  } else { // CurseForge
    int lt = curseforge_loader_id(NewModState->loader);
    // 只有 mc_version 非空才加 gameVersion 过滤
    char gameVer_param[96] = "";
    if (NewModState->mc_version[0])
      snprintf(gameVer_param, sizeof(gameVer_param), "gameVersion=%s",
               NewModState->mc_version);
    // loader 参数：前置有 gameVersion 时加 &，否则直接写（作为首个参数不加 &）
    char loader_param[32] = "";
    if (lt > 0)
      snprintf(loader_param, sizeof(loader_param), "%smodLoaderType=%d",
               gameVer_param[0] ? "&" : "", lt);
    snprintf(cmd, sizeof(cmd),
             "curl -s -m 20 -o /tmp/tmcl_cf_mod_version.json "
             "-H 'x-api-key: %s' "
             "\"https://api.curseforge.com/v1/mods/%s/files?"
             "%s%s\"",
             CURSEFORGE_API_KEY, NewModState->mod_id,
             gameVer_param, loader_param);
    tmp_file = "/tmp/tmcl_cf_mod_version.json";
  }

  if (system(cmd) != 0) {
    clear();
    printw("FIELD to download !!!");
    printw("Press any key to continue");
    getch();
    return 1;
  }

  // ----- 统一解析（含过滤）-----
  state->mod_version_root = parse_json(tmp_file);
  if (!state->mod_version_root) {
    clear();
    printw("FIELD to parse json file !!!");
    printw("Press any key to continue");
    getch();
    return 1;
  }

  int src = (strcmp(NewModState->source, "Modrinth") == 0)
                ? SOURCE_MODRINTH
                : SOURCE_CURSEFORGE;
  state->count_all = mod_api_parse_versions(state->mod_version_root, src,
                                            state->mc_version,
                                            state->modloader,
                                            &state->versions);
  state->count_flitered = state->count_all;

  if (state->count_flitered == 0) {
    clear();
    printw("There is nothing to download !!!");
    printw("Press any key to continue");
    getch();
    return 1;
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

  // ---------- 版本列表 ----------
  if (state->versions && state->count_flitered > 0) {
    for (int i = 0; i < state->count_flitered && 2 * i <= row; i++) {
      int idx = i + state->scroll_offset;
      if (idx >= state->count_flitered)
        break;
      ModVersion *mv = &state->versions[idx];
      bool selected = (idx == state->selected);
      if (selected)
        attron(A_REVERSE);
      mvprintw(2 + 2 * i, 0, "%-*s", col, " ");
      mvprintw(3 + 2 * i, 0, "%-*s", col, " ");

      // 第一行: 类型+版本号+下载量
      attron(A_BOLD);
      mvprintw(2 + 2 * i, 0, " %2d ", idx);
      // release_type 着色
      if (mv->release_type >= 0 && mv->release_type <= 2) {
        const char *type_char = "RBA";
        attron(COLOR_PAIR(modloader_color("fabric", selected)));
        printw("%c", type_char[mv->release_type]);
        attroff(COLOR_PAIR(modloader_color("fabric", selected)));
      } else {
        printw("U");
      }
      printw(" %s", mv->version_number);
      attroff(A_BOLD);
      printw("  D:%d", mv->downloads);

      // 第二行: game_versions + loaders
      move(3 + 2 * i, 4);
      printw("%s %s", mv->game_versions, mv->loaders);

      if (selected)
        attroff(A_REVERSE);
    }
  } else {
    mvprintw(2, 3, "There is no version to install");
    mvprintw(3, 3,
             "You may change your filter conditions (minecraft version and "
             "modloader) below.");
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

void aaction_install(ModVersionState *state) {
  ModVersion *mv = &state->versions[state->selected];
  char cmd[4096];  // dest(1279) + download_url(1023) + 命令字面量
  char dest[1280]; // mods_dir(1023) + "/" + filename(255) + NUL = 1280
  snprintf(dest, sizeof(dest), "%s/%s", state->mods_dir, mv->filename);

  snprintf(cmd, sizeof(cmd), "curl -# -L -m 60 -o \"%s\" \"%s\"",
           dest, mv->download_url);
  endwin();
  // 执行下载
  printf("\n");
  if (system(cmd) != 0) {
    // 处理下载失败
    printf("\nFIELD ! you may check your network.");
  } else {
    printf("\nSuccess!");
  }
  printf("\nPress [ENTER] to return.");
  getchar();
  refresh();
}

// ========== 主函数 ==========
void mod_version_page(ModVersionState *state, NewModState *NewModState) {
  if (mod_version_init(state, NewModState))
    return;
  int pre_selected = -1;

  while (1) {
    int row, col;
    getmaxyx(stdscr, row, col);
    (void)col;

    if (state->selected != pre_selected && state->count_flitered > 0) {
      ModVersion *mv = &state->versions[state->selected];
      snprintf(state->install_as, sizeof(state->install_as), "%s/%s",
               state->mods_dir, mv->filename);
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
      free(state->versions);
      state->versions = NULL;
      return;
      break;
    }

    switch (action) {
    case 0:
      aaction_install(state);
      free(state->versions);
      state->versions = NULL;
      return;
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
