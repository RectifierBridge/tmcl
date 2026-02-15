#include "config.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void reset_config(ConfigState *state);
void change_config(ConfigState *state);
void config_from_json(cJSON *json, ConfigState *state);
void config_init(ConfigState *state) {
  // 初始化配置项
  char *home_dir = getenv("HOME");
  strcpy(state->home_dir, home_dir);
  state->item_count = 6;
  state->items = malloc(state->item_count * sizeof(ConfigItem));

  // 打开tmcl目录
  char tmcl_dir[1024];
  char tmcl_config_path[1034];
  snprintf(tmcl_dir, sizeof(tmcl_dir), "%s/.tmcl", home_dir);
  snprintf(tmcl_config_path, sizeof(tmcl_config_path), "%s/tmcl.json",
           tmcl_dir);
  strcpy(state->tmcl_dir, tmcl_dir);
  strcpy(state->tmcl_config_path, tmcl_config_path);

  char game_dir[1024];
  snprintf(game_dir, sizeof(game_dir), "%s/.minecraft", home_dir);
  char *keys[] = {"java_path", "memory",          "game_dir",
                  "jvm_args",  "download_cource", "mod_source"};
  char *default_values[] = {"/usr/bin/java", "2048",      game_dir, "",
                            "BMCLAPI",       "curseforge"};

  for (int i = 0; i < state->item_count; i++) {
    strcpy(state->items[i].key, keys[i]);
    strcpy(state->items[i].defaule_value, default_values[i]);
  }
  DIR *tmcl_dirp = opendir(tmcl_dir);
  if (tmcl_dirp == NULL) {
    mkdir(tmcl_dir, 0755);
  }

  // 读取文件内容
  FILE *file = fopen(tmcl_config_path, "r");

  bool need_write = 0;
  if (file != NULL) {
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 读取文件内容
    char *file_content = malloc(file_size + 1);

    fread(file_content, 1, file_size, file);
    file_content[file_size] = '\0';
    fclose(file);

    // 解析 JSON
    cJSON *root = cJSON_Parse(file_content);
    free(file_content);

    if (root == NULL) {
      for (int i = 0; i < state->item_count; i++) {
        strcpy(state->items[i].value, default_values[i]);
      }
      need_write = 1;
    } else {
      // 从 cJSON 对象创建配置结构体
      // 从 JSON 提取值
      cJSON *item;
      for (int i = 0; i < state->item_count; i++) {
        cJSON *item = cJSON_GetObjectItem(root, keys[i]);
        if (item == NULL) {
          strcpy(state->items[i].value, default_values[i]);
          need_write = 1;
        } else {
          strcpy(state->items[i].value, item->valuestring);
        }
      }
    }
    cJSON_Delete(root);
  } else {
    for (int i = 0; i < state->item_count; i++) {
      strcpy(state->items[i].value, default_values[i]);
    }
    need_write = 1;
  }

  if (need_write == 1) {
    config_write(state);
  }
  state->selected_item = 0;
  state->scroll_offset = 0;
}

void config_page(int ch, int *middlep, ConfigState *state) {
  clear();
  int row = 0, col = 0;
  getmaxyx(stdscr, row, col);

  // 处理配置项选择
  switch (ch) {
  case 'j': // 向下选择
    if (state->selected_item < state->item_count - 1) {
      state->selected_item++;
      int visible_rows = row - 10;
      if (state->selected_item >= state->scroll_offset + visible_rows) {
        state->scroll_offset++;
      }
    }
    break;
  case 'k': // 向上选择
    if (state->selected_item > 0) {
      state->selected_item--;
      if (state->selected_item < state->scroll_offset) {
        state->scroll_offset--;
      }
    }
    break;
  case 'c': // 修改配置
    if (state->selected_item >= 0 && state->selected_item < state->item_count) {
      change_config(state);
    }
    break;
  case 'r': // 修改配置
    if (state->selected_item >= 0 && state->selected_item < state->item_count) {
      reset_config(state);
      break;
    case 'h':                        // 在操作间向左移动
      *middlep = (*middlep + 1) % 2; // 循环向左（2个操作）
      break;
    case 'l':                        // 在操作间向右移动
      *middlep = (*middlep + 1) % 2; // 循环向右（2个操作）
      break;
    case '\n':
      switch (*middlep) {
      case 0:
        change_config(state);
        break;
      case 1:
        reset_config(state);
        break;
      }
      break;
    }
  }

  mvprintw(2, 2, "Key              Value");
  mvprintw(3, 2, "---              -----");
  // 显示配置项列表
  int start_y = 4;
  int max_display = row - start_y - 3;

  for (int i = state->scroll_offset;
       i < state->item_count && i < state->scroll_offset + max_display; i++) {

    move(start_y + i - state->scroll_offset, 2);

    if (i == state->selected_item) {
      attron(A_REVERSE);
    }

    printw("%-15s: %-20s", state->items[i].key, state->items[i].value);

    if (i == state->selected_item) {
      attroff(A_REVERSE);
    }
  }

  // 显示滚动提示
  if (state->scroll_offset > 0) {
    mvprintw(start_y - 1, 2, "...more above...");
  }
  if (state->scroll_offset + max_display < state->item_count) {
    mvprintw(start_y + max_display, 2, "...more below...");
  }

  // 底部操作提示
  switch (*middlep) {
  case 0:
    attron(A_REVERSE);
    mvprintw(row - 1, 0, "[c]hange");
    attroff(A_REVERSE);
    printw(" [r]eset");
    break;
  case 1:
    mvprintw(row - 1, 0, "[c]hange ");
    attron(A_REVERSE);
    printw("[r]eset");
    attroff(A_REVERSE);
    break;
  }

  // 页面标题和导航
  move(0, 0);
  printw("[V]ersion ");
  attron(A_REVERSE);
  printw("[C]onfig");
  attroff(A_REVERSE);
  printw(" [A]ccount");
  mvprintw(0, col - 15, "tap [q] to quit");
}

void change_config(ConfigState *state) {

  int row, col;
  getmaxyx(stdscr, row, col);

  if (state->selected_item < 4) {
    echo(); // 开启回显
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[state->selected_item].key);
    mvprintw(5, 2, "Current config: %s",
             state->items[state->selected_item].value);
    mvprintw(9, 2, "Tap nothing but enter to cancle");
    mvprintw(7, 2, "Enter new config: ");

    char new_config[64];
    getnstr(new_config, sizeof(new_config) - 1);
    if ((strcmp(new_config, "")) != 0) {
      strcpy(state->items[state->selected_item].value, new_config);
      config_write(state);
    }
    noecho(); // 关闭回显
    curs_set(0);

    // 清除重命名界面，主循环会重新绘制完整界面
    clear();
  }
}

void reset_config(ConfigState *state) {
  // int row, col;
  // getmaxyx(stdscr, row, col);

  echo();      // 开启回显
  curs_set(1); // 显示光标
  clear();

  mvprintw(3, 2, "reset config: %s", state->items[state->selected_item].key);
  mvprintw(5, 2, "Current config: %s",
           state->items[state->selected_item].value);
  mvprintw(7, 2, "reset to default: %s",
           state->items[state->selected_item].defaule_value);
  mvprintw(9, 2, "Do you want to reset[y/N]");
  char c = getch();
  switch (c) {
  case 'y':
    strcpy(state->items[state->selected_item].value,
           state->items[state->selected_item].defaule_value);
    config_write(state);
    break;
  default:
    break;
  }
  noecho(); // 关闭回显
  curs_set(0);

  // 清除重命名界面，主循环会重新绘制完整界面
  clear();
  return;
}

void config_write(ConfigState *state) {
  cJSON *root = cJSON_CreateObject();

  for (int i = 0; i < state->item_count; i++) {
    cJSON_AddStringToObject(root, state->items[i].key, state->items[i].value);
  }

  char *json_string = cJSON_Print(root);

  // 写入文件
  FILE *file = fopen(state->tmcl_config_path, "w");
  if (file == NULL) {
    // failed
    free(json_string);
    cJSON_Delete(root);
  }

  fputs(json_string, file);
  fclose(file);

  // 清理内存
  free(json_string);
  cJSON_Delete(root);
}

// JSON字符串转义
void json_escape_string(const char *input, char *output, size_t output_size) {
  if (input == NULL || output == NULL)
    return;

  size_t j = 0;
  for (size_t i = 0; input[i] != '\0' && j < output_size - 1; i++) {
    switch (input[i]) {
    case '"':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = '"';
      }
      break;
    case '\\':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = '\\';
      }
      break;
    case '\n':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = 'n';
      }
      break;
    case '\t':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = 't';
      }
      break;
    case '\r':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = 'r';
      }
      break;
    case '\b':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = 'b';
      }
      break;
    case '\f':
      if (j + 2 < output_size) {
        output[j++] = '\\';
        output[j++] = 'f';
      }
      break;
    default:
      output[j++] = input[i];
      break;
    }
  }
  output[j] = '\0';
}

void config_cleanup(ConfigState *state) {
  if (state->items) {
    free(state->items);
    state->items = NULL;
  }
  state->item_count = 0;
}
