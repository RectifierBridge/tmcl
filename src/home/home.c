//  home.c
#include "home.h"
#include "../version/version.h"
#include <cjson/cJSON.h>
#include <ncurses.h>
// #include <string.h>

void home_init(HomeState *state, VersionState *VersionState) {
  state->selected = 0;
}

void home_page(int ch, int *middlep, HomeState *state,
               VersionState *VersionState) {
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

  // two game versions to display

  // key
  switch (ch) {
  case 'j':
    state->selected = 1;
  case 'k':
    state->selected = 0;
  }
}
