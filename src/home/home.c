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
    if (state->pinned_index < 0 && strcmp(VersionState->versions[i].name,
                                          ConfigState->items[9].value) == 0) {
      state->pinned_index = i;
    }
    if (state->last_index < 0 && strcmp(VersionState->versions[i].name,
                                        ConfigState->items[10].value) == 0) {
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
  int cur_row, cur_col;

  // print hellow
  printw("\n\nWelcome to:\n\n");
  printw(" ########  ###    ###  #######  ##    \n");
  printw("    ##     ####  ####  ##       ##    \n");
  printw("    ##     ## #### ##  ##       ##    \n");
  printw("    ##     ##  ##  ##  ##       ##    \n");
  printw("    ##     ##      ##  #######  ######\n\n\n");
  printw(
      "  Terminal Minecraft Launcher (TMCL) is a lightweight TUI-based "
      "launcher "
      "for Minecraft: Java Edition, developed primarily for GNU/Linux by aleph "
      "in C. TMCL is free software released under the GPL-3.0 license—you are "
      "free to redistribute or fork it, as long as you give proper credit to "
      "the original source.\n"
      "  This is the TMCL homepage, where you can quickly "
      "launch a fixed game version or the one you last played. Navigation "
      "follows Vim‑style keybindings: use h/j/k/l to move the cursor, H/L to "
      "shift the top navigation bar cursor, and press the corresponding key "
      "shown in brackets, or select an action from the bottom bar and hit "
      "Enter to execute it.\n"
      "  TMCL currently supports installing and launching vanilla, Fabric, "
      "Forge, and NeoForge game versions. It can load mods, "
      "but does not support mod management, shader packs, resource packs, "
      "modpacks, automatic OptiFine installation, etc.\n"
      "Please abide by Microsoft's relevant regulations regarding Minecraft-we "
      "support legitimate copies."
      " The copyright of Minecraft belongs to Mojang Studios and Microsoft. "
      "The software producer is not responsible for any copyright issues "
      "arising from the use of TMCL. Please support the official game. Users "
      "are obligated to ensure that they possess a legitimate account when "
      "using TMCL. All "
      "consequences arising from the use of TMCL by the user shall be borne by "
      "the user himself. Any legal disputes and conflicts involving TMCL have "
      "nothing to do with the developer, and TMCL and the developer will not "
      "bear any responsibility.\n"
      "  The original project repository is at "
      "https://codeberg.org/furii/tmcl. If you like TMCL, feel free to leave a "
      "star, and we welcome suggestions or bug reports.\n"
      "  Currently, the only contributor to TMCL is aleph0 (furii), a "
      "free-software enthusiast from China. "
      "He is the author of TMCL and the founder of aleph. When he started this "
      "project, he was just a high school student with extremely limited "
      "coding skills, little relevant experience, and not much spare time-so "
      "please bear with us. That said, you are also welcome to join us.\n"
      "  Buy us a coffee-it really means a lot to us, even a single dollar "
      "counts.\n\n");
  getyx(stdscr, cur_row, cur_col);
  mvprintw(cur_row, col - 27, "contact:");
  mvprintw(cur_row + 1, col - 27, "aleph0: fossrina@163.com");

  // key
  switch (ch) {
  case 'j':
  case KEY_DOWN:
    state->selected = 1;
    break;
  case 'k':
  case KEY_UP:
    state->selected = 0;
    break;
  case 'b':
  case '\n': {
    // 启动选中的版本（Pinned 或 Last Play）
    int target =
        (state->selected == 0) ? state->pinned_index : state->last_index;
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
  move(4, 45);
  printw("Pinned Version:\n");
  if (state->selected == 0) {
    attron(A_REVERSE);
  }
  if (VersionState->version_count == 0) {
    mvprintw(5, 45, "No version installed.");
  } else if (state->pinned_index < 0) {
    mvprintw(5, 45, "No pinned version.");
  } else {
    mvprintw(5, 45, "%s", VersionState->versions[state->pinned_index].name);
  }
  if (state->selected == 0) {
    attroff(A_REVERSE);
  }

  move(7, 45);
  printw("Last Play:\n");
  if (state->selected == 1) {
    attron(A_REVERSE);
  }
  if (state->last_index >= 0) {
    mvprintw(8, 45, "%s", VersionState->versions[state->last_index].name);
  } else {
    mvprintw(8, 45, "not play yet.");
  }
  if (state->selected == 1) {
    attroff(A_REVERSE);
  }
  mvprintw(row - 2, 0,
           "                                                                   "
           "                                                                   "
           "                                                                   "
           "                                                                   "
           "                                                                   "
           "                                                  ");
  mvprintw(row - 1, 0, "tap [Enter] or [b] to begin");
}
