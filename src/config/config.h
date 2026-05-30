#ifndef CONFIG_H
#define CONFIG_H

#include <ncurses.h>
//"java_path", "memory", "game_dir", "jvm_args",  "download_cource", "mod_source"
// 配置项结构
typedef struct {
  char key[32];
  char value[128];
  char defaule_value[128];
} ConfigItem;

// Config 页面状态
typedef struct {
  ConfigItem *items;
  int item_count;
  int selected_item;
  int scroll_offset;
  char home_dir[1000];
  char tmcl_dir[1024];
  char tmcl_config_path[1034];
} ConfigState;

// 函数声明
void config_init(ConfigState *state);
void config_page(int ch, int *middlep, ConfigState *state);
void config_cleanup(ConfigState *state);
void config_write(ConfigState *state);

#endif
