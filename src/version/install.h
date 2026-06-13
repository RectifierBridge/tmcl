#ifndef INSTALL_H
#define INSTALL_H

#include "../config/config.h"
#include "../version/version.h"
#include <ncurses.h>

// 安装 UI 状态
typedef struct {
  int selected_row;      // 当前选中的行 (0=type, 1=version, 2=name, 3=modloader, 4=install)
  int type_sel;          // type 子菜单的选中项
  int version_sel;       // version 列表的选中项
  int version_scroll;    // version 列表的滚动偏移
  int versions_count;    // 从 manifest 拉取的版本数量
  char **version_list;   // 版本 id 数组
  char **version_urls;   // 对应的版本 JSON URL 数组
} InstallState;

// 安装页面主函数
void install_page(VersionState *vstate, ConfigState *cstate);

#endif
