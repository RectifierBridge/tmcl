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
    if (strcmp(VersionState->versions[i].name, ConfigState->items[6].value) ==
        0) {
      state->pinned_index = i;
      break;
    }
    if (strcmp(VersionState->versions[i].name, ConfigState->items[7].value) ==
        0) {
      state->last_index = i;
      break;
    }
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
  printw("  TMCL is free open-source software.\n\n\n\n");
  // 底部信息
  mvprintw(row - 4, 2, "  Buy us a coffee.");
  mvprintw(row - 3, 2, "  Developer: fossrina.");

  // key
  switch (ch) {
  case 'j':
    state->selected = 1;
    break;
  case 'k':
    state->selected = 0;
    break;
  }

  // two game versions to display
  int line_start = 20;

  move(line_start, 4);
  if (state->selected == 0) {
    attron(A_REVERSE);
  }
  if (VersionState->version_count == 0) {
    printw("No version installed.");
  } else if (state->pinned_index < 0) {
    printw("No pinned version.");
  } else {
    printw("%s", VersionState->versions[state->pinned_index].name);
  }
  if (state->selected == 0) {
    attroff(A_REVERSE);
  }

  move(line_start + 5, 4);
  if (state->selected == 1) {
    attron(A_REVERSE);
  }
  if (state->last_index >= 0) {
    printw("%s", VersionState->versions[state->last_index].name);
  } else {
    printw("not play yet.");
  }
  if (state->selected == 1) {
    attroff(A_REVERSE);
  }
}
