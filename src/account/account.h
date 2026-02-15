#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <ncurses.h>

// 账户信息结构
typedef struct {
  char username[32];
  char uuid[64];
  char type[16]; // offline, mojang, microsoft
  int selected;  // 当前选中的账户
} AccountInfo;

// Account 页面状态
typedef struct {
  AccountInfo *accounts;
  int account_count;
  int selected_account;
  int scroll_offset;
} AccountState;

// 函数声明
void account_init(AccountState *state);
void account_page(int ch, int *middlep, AccountState *state);
void account_write(AccountState *state);
void account_cleanup(AccountState *state);

#endif
