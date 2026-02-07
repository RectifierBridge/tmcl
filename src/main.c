// main.c
#include "account.h"
#include "config.h"
#include "version.h"
#include <ncurses.h>

// 函数声明

int main() {
  int ch = 'V';
  int bigp = 0;
  int middlep = 0;

  // 初始化所有页面的状态
  VersionState version_state = {0};
  ConfigState config_state = {0};
  AccountState account_state = {0};

  initscr();
  curs_set(0); // 隐藏光标
  clear();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);

  // 初始化所有页面数据
  config_init(&config_state);
  account_init(&account_state);
  init_versions(&version_state, &config_state);

  while (1) {
    int bigpp = bigp;
    switch (ch) {
    case 'V':
      bigp = 0;
        init_versions(&version_state, &config_state);
      break;
    case 'C':
      bigp = 1;
      if (bigpp == 1) {
        config_init(&config_state);
      }
      break;
    case 'A':
      bigp = 2;
      break;
    case 'L':
      bigp = (bigp + 1) % 3;
      break;
    case 'H':
      bigp = (bigp + 2) % 3;
      break;
    case 'q':
      // 清理所有资源
      free_versions(&version_state);
      config_cleanup(&config_state);
      account_cleanup(&account_state);
      endwin();
      return 0;
    }
    if (bigpp != bigp) {
      middlep = 0;
    }
    // if (bigpp == 1 && bigp != 1) {
    //   config_write(&config_state);
    // }
    //
    // if (bigpp == 2 && bigp != 2) {
    //   account_write(&account_state);
    // }

    if (bigp == 0 && bigpp != 0) {
      init_versions(&version_state, &config_state);
    }

    switch (bigp) {
    case 0:
      version_page(ch, &middlep, &version_state, &config_state);
      break;
    case 1:
      config_page(ch, &middlep, &config_state);
      break;
    case 2:
      account_page(ch, &middlep, &account_state);
      break;
    }

    ch = getch();
  }

  endwin();
  return 0;
}
