#ifndef CONFIG_H
#define CONFIG_H

#include <ncurses.h>
//"java_path", "memory", "game_dir", "jvm_args",  "download_cource",
//"mod_source", "pinned_version", "last_play"
// 配置项结构
typedef struct {
  char key[32];
  char value[128];
  char defaule_value[128];
} ConfigItem;

// 用户添加的 Java 路径（tmcl.json 中 java_paths 数组，不展示在设置列表）
#define MAX_JAVA_PATHS 32
#define JAVA_PATH_LEN 512

// Config 页面状态
typedef struct {
  ConfigItem *items;
  int item_count;
  int selected_item;
  int scroll_offset;
  char home_dir[1000];
  char tmcl_dir[1024];
  char tmcl_config_path[1034];
  char user_java_paths[MAX_JAVA_PATHS][JAVA_PATH_LEN]; // 用户手动添加的 java 路径
  int user_java_path_count;
} ConfigState;

// 函数声明
void config_init(ConfigState *state);
void config_page(int ch, int *middlep, ConfigState *state);
void config_cleanup(ConfigState *state);
void config_write(ConfigState *state);
int config_get_memory_mb(ConfigState *state);

// 通过 key 字符串读写配置项，避免硬编码索引
const char *config_get(ConfigState *state, const char *key);
void config_set(ConfigState *state, const char *key, const char *value);

#endif
