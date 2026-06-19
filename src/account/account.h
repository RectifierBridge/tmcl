#ifndef ACCOUNT_H
#define ACCOUNT_H

#include "../config/config.h"
#include <ncurses.h>

// 账户信息结构
typedef struct {
  char username[32];       // 游戏内显示名称
  char uuid[64];           // Minecraft UUID（带连字符）
  char type[16];           // offline / mojang / microsoft / third_party
  char access_token[512];  // OAuth2 / Yggdrasil access token
  char refresh_token[512]; // Microsoft refresh token
  char auth_server[256];   // 第三方 Yggdrasil API 根 URL（仅 third_party）
  char skin_path[256];     // 离线模式自定义皮肤文件路径
  int selected;            // 当前选中的账户
} AccountInfo;

// Account 页面状态
typedef struct {
  AccountInfo *accounts;
  int account_count;
  int selected_account;
  int scroll_offset;
  char accounts_file[1034]; // ~/.tmcl/accounts.json 路径
} AccountState;

// 函数声明
void account_init(AccountState *state, ConfigState *cstate);
void account_page(int ch, int *middlep, AccountState *state);
void account_write(AccountState *state);
void account_cleanup(AccountState *state);
// 获取当前选中账户（供 begin_version 使用）
AccountInfo *account_get_selected(AccountState *state);
// 全局指针，由 main.c 设置
extern AccountState *g_account_state;

#endif
