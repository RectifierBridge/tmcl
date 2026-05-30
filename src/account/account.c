#include "account.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

void account_init(AccountState *state) {
  // 初始化账户数据
  state->account_count = 2;
  state->accounts = malloc(state->account_count * sizeof(AccountInfo));

  if (state->accounts != NULL) {
    // 离线账户 1
    strcpy(state->accounts[0].username, "Player1");
    strcpy(state->accounts[0].uuid, "off user 1");
    strcpy(state->accounts[0].type, "offline");
    state->accounts[0].selected = 1;

    // 离线账户 2
    strcpy(state->accounts[1].username, "Steve");
    strcpy(state->accounts[1].uuid, "off user 2");
    strcpy(state->accounts[1].type, "offline");
    state->accounts[1].selected = 0;
  }

  state->selected_account = 0;
  state->scroll_offset = 0;
}

void account_page(int ch, int *middlep, AccountState *state) {
  clear();
  int row = 0, col = 0;
  getmaxyx(stdscr, row, col);

  // 页面标题和导航
  move(0, 0);
  printw("[V]ersion [C]onfig ");
  attron(A_REVERSE);
  printw("[A]ccount");
  attroff(A_REVERSE);
  mvprintw(0, col - 15, "tap [q] to quit");

  // 处理账户选择导航
  switch (ch) {
  case 's':
    // 选择当前账户
    if (state->selected_account >= 0 &&
        state->selected_account < state->account_count) {
      // 取消之前的选择
      for (int i = 0; i < state->account_count; i++) {
        state->accounts[i].selected = 0;
      }
      // 选择当前账户
      state->accounts[state->selected_account].selected = 1;
    }
    *middlep = 0;
    break;
  case 'n':
    *middlep = 1;
    break;
  case 'd':
    *middlep = 2;
    break;
  case 'h':
    *middlep = (*middlep + 2) % 3;
    break;
  case 'l':
    *middlep = (*middlep + 1) % 3;
    break;
  case 'j': // 向下选择
    if (state->selected_account < state->account_count - 1) {
      state->selected_account++;
      int visible_rows = row - 10;
      if (state->selected_account >= state->scroll_offset + visible_rows) {
        state->scroll_offset++;
      }
    }
    break;
  case 'k': // 向上选择
    if (state->selected_account > 0) {
      state->selected_account--;
      if (state->selected_account < state->scroll_offset) {
        state->scroll_offset--;
      }
    }
    break;
  }

  mvprintw(2, 2, "    Type       Name");
  mvprintw(3, 2, "    ----       ----");
  // 显示账户列表
  int start_y = 4;
  int max_display = row - start_y - 3;

  for (int i = state->scroll_offset;
       i < state->account_count && i < state->scroll_offset + max_display;
       i++) {

    move(start_y + i - state->scroll_offset, 2);

    if (i == state->selected_account) {
      attron(A_REVERSE);
    }

    // 显示账户状态和基本信息
    char status[4];
    if (state->accounts[i].selected) {
      strcpy(status, "[*]");
    } else {
      strcpy(status, "[ ]");
    }

    printw("%s %s    %s", status, state->accounts[i].type,
           state->accounts[i].username);

    if (i == state->selected_account) {
      attroff(A_REVERSE);
    }
  }

  // 显示滚动提示
  if (state->scroll_offset > 0) {
    mvprintw(start_y - 1, 2, "...more above...");
  }
  if (state->scroll_offset + max_display < state->account_count) {
    mvprintw(start_y + max_display, 2, "...more balow...");
  }

  // 底部操作提示
  switch (*middlep) {
  case 0:
    attron(A_REVERSE);
    mvprintw(row - 1, 0, "[s]elect");
    attroff(A_REVERSE);
    printw(" [n]ew [d]elete");
    break;
  case 1:
    mvprintw(row - 1, 0, "[s]elect ");
    attron(A_REVERSE);
    printw("[n]ew");
    attroff(A_REVERSE);
    printw(" [d]elete");
    break;
  case 2:
    mvprintw(row - 1, 0, "[s]elect [n]ew ");
    attron(A_REVERSE);
    printw("[d]elete");
    attroff(A_REVERSE);
    break;
  }
}

void account_write(AccountState *state) {}

void account_cleanup(AccountState *state) {
  if (state->accounts) {
    free(state->accounts);
    state->accounts = NULL;
  }
  state->account_count = 0;
}
