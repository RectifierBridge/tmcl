// main.c
#include "account/account.h"
#include "config/config.h"
#include "home/home.h"
#include "job/job.h"
#include "version/version.h"
#include <ncurses.h>

// 函数声明
void title_bar(int bigp);

int main() {
  int ch = 'T';
  int bigp = 0;
  int middlep = 0;

  // 初始化所有页面的状态
  HomeState home_state = {0};
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
  account_init(&account_state, &config_state);
  init_versions(&version_state, &config_state);

  while (1) {
    int bigpp = bigp;
    switch (ch) {
    case 'T':
      bigp = 0;
      home_init(&home_state, &version_state, &config_state);
      break;
    case 'V':
      bigp = 1;
      init_versions(&version_state, &config_state);
      break;
    case 'C':
      bigp = 2;
      if (bigpp == 2) {
        config_init(&config_state);
      }
      break;
    case 'A':
      bigp = 3;
      break;
    case 'L':
      bigp = (bigp + 1) % 4;
      break;
    case 'H':
      bigp = (bigp + 3) % 4;
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

    if (bigp == 1 && bigpp != 1) {
      init_versions(&version_state, &config_state);
    }

    // 如果 begin_version / do_install 调用了 endwin()，重启 ncurses
    if (isendwin()) {
      initscr();
      curs_set(0);
      cbreak();
      noecho();
      keypad(stdscr, TRUE);
      clear();
      init_versions(&version_state, &config_state);
      home_init(&home_state, &version_state, &config_state);
    }

    switch (bigp) {
    case 0:
      home_page(ch, &middlep, &home_state, &version_state, &config_state);
      title_bar(bigp);
      break;
    case 1:
      version_page(ch, &middlep, &version_state, &config_state);
      title_bar(bigp);
      break;
    case 2:
      config_page(ch, &middlep, &config_state);
      title_bar(bigp);
      break;
    case 3:
      account_page(ch, &middlep, &account_state);
      title_bar(bigp);
      break;
    }

    ch = getch();
  }

  endwin();
  return 0;
}

void title_bar(int bigp) {
  int row, col;
  getmaxyx(stdscr, row, col);

  switch (bigp) {
  case 0:
    move(0, 0);
    attron(A_REVERSE);
    printw("[T]mcl");
    attroff(A_REVERSE);
    printw(" [V]ersion [C]onfig [A]ccount");
    mvprintw(0, col - 15, "tap [q] to quit");
    break;
  case 1:
    move(0, 0);
    printw("[T]mcl ");
    attron(A_REVERSE);
    printw("[V]ersion");
    attroff(A_REVERSE);
    printw(" [C]onfig [A]ccount");
    mvprintw(0, col - 15, "tap [q] to quit");
    break;
  case 2:
    move(0, 0);
    printw("[T]mcl [V]ersion ");
    attron(A_REVERSE);
    printw("[C]onfig");
    attroff(A_REVERSE);
    printw(" [A]ccount");
    mvprintw(0, col - 15, "tap [q] to quit");
    break;
  case 3:
    move(0, 0);
    printw("[T]mcl [V]ersion [C]onfig ");
    attron(A_REVERSE);
    printw("[A]ccount");
    attroff(A_REVERSE);
    mvprintw(0, col - 15, "tap [q] to quit");
    break;
  }
};
