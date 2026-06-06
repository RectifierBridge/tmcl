//  home.c
#include "home.h"
#include "../config/config.h"
#include "../version/version.h"
#include <cjson/cJSON.h>
#include <ncurses.h>
#include <string.h>

void home_init(HomeState *state, VersionState *VersionState,
               ConfigState *ConfigState) {
  state->selected = 0;
  state->pinned_index = -1;
  state->last_index = -1;

  // look for pinned_index and last_index
  for (int i = 0; i < VersionState->version_count; i++) {
    if (state->pinned_index < 0 &&
        strcmp(VersionState->versions[i].name, ConfigState->items[6].value) ==
            0) {
      state->pinned_index = i;
    }
    if (state->last_index < 0 &&
        strcmp(VersionState->versions[i].name, ConfigState->items[7].value) ==
            0) {
      state->last_index = i;
    }
    if (state->pinned_index >= 0 && state->last_index >= 0)
      break;
  }
}

void home_page(int ch, int *middlep, HomeState *state,
               VersionState *VersionState, ConfigState *ConfigState) {
  clear();

  int row, col;
  getmaxyx(stdscr, row, col);

  // print hellow
  printw("\n\n  Welcome to:\n\n");
  printw(" ########  ###    ###  #######  ##    \n");
  printw("    ##     ####  ####  ##       ##    \n");
  printw("    ##     ## #### ##  ##       ##    \n");
  printw("    ##     ##  ##  ##  ##       ##    \n");
  printw("    ##     ##      ##  #######  ######\n\n");
  printw("  Terminal Minecraft Launcher,\n");
  printw("  A Minecraft launcher based on TUI.\n");
  printw("  TMCL is free open-source software.\n\n");
  // 底部信息
  printw("  Buy us a coffee.\n");
  printw("  Developer: fossrina.");

  // key
  switch (ch) {
  case 'j':
    state->selected = 1;
    break;
  case 'k':
    state->selected = 0;
    break;
  case '\n': {
    // 启动选中的版本（Pinned 或 Last Play）
    int target = (state->selected == 0) ? state->pinned_index
                                        : state->last_index;
    if (target >= 0 && target < VersionState->version_count) {
      int saved = VersionState->selected_version;
      VersionState->selected_version = target;
      begin_version(VersionState, ConfigState);
      VersionState->selected_version = saved;
      // 刷新 last_play 指向刚刚启动的版本
      state->last_index = target;
      // 重新初始化 home 状态以同步 config 中的变更
      home_init(state, VersionState, ConfigState);
    }
    break;
  }
  }

  // two game versions to display
  move(19, 4);
  printw("Pinned Version:\n");
  if (state->selected == 0) {
    attron(A_REVERSE);
  }
  if (VersionState->version_count == 0) {
    mvprintw(20, 4,"No version installed.");
  } else if (state->pinned_index < 0) {
    mvprintw(20, 4, "No pinned version.");
  } else {
    mvprintw(20, 4, "%s", VersionState->versions[state->pinned_index].name);
  }
  if (state->selected == 0) {
    attroff(A_REVERSE);
  }

  move(22, 4);
  printw("Last Play:\n");
  if (state->selected == 1) {
    attron(A_REVERSE);
  }
  if (state->last_index >= 0) {
    mvprintw(23, 4, "%s", VersionState->versions[state->last_index].name);
  } else {
    mvprintw(23, 4, "not play yet.");
  }
  if (state->selected == 1) {
    attroff(A_REVERSE);
  }
  mvprintw(row - 1, 0, "tap [Enter] to start");
}
