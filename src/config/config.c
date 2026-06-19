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
static int detect_memory_mb(void);
void config_init(ConfigState *state) {
  // 初始化配置项
  char *home_dir = getenv("HOME");
  strcpy(state->home_dir, home_dir);
  state->item_count = 9;
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
  char *keys[] = {"java_path",      "memory",          "game_dir",
                  "jvm_args",       "download_cource", "mod_source",
                  "threads",        "pinned_version",  "last_play"};
  char *default_values[] = {"/usr/bin/java", "auto",       game_dir, "",
                            "Official",       "CurseForge", "96",     "", ""};

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

  // 如果 memory 值为 "auto"，展开为 "auto(实际数值)"
  if (strcmp(state->items[1].value, "auto") == 0) {
    int mb = detect_memory_mb();
    snprintf(state->items[1].value, sizeof(state->items[1].value), "auto(%d)",
             mb);
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
    if (state->selected_item < state->item_count - 3) {
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

  mvprintw(2, 3, "Key              Value");
  mvprintw(3, 3, "---              -----");
  // 显示配置项列表（最后两项 pinned_version / last_play 由系统管理，不展示）
  int start_y = 4;
  int display_items = state->item_count - 2;
  int max_display = row - start_y - 3;

  for (int i = state->scroll_offset;
       i < display_items && i < state->scroll_offset + max_display; i++) {

    move(start_y + i - state->scroll_offset, 2);

    if (i == state->selected_item) {
      attron(A_REVERSE);
    }

    printw(" %-15s  %-20s", state->items[i].key, state->items[i].value);

    if (i == state->selected_item) {
      attroff(A_REVERSE);
    }
  }

  // 显示滚动提示
  if (state->scroll_offset > 0) {
    mvprintw(start_y - 1, 2, "...more above...");
  }
  if (state->scroll_offset + max_display < display_items) {
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

// 自动检测系统可用内存并返回合适的 Minecraft 内存分配（MB）
static int detect_memory_mb() {
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f) return 2048;
  char line[256];
  long mem_kb = 0;
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "MemAvailable: %ld", &mem_kb) == 1) break;
    if (sscanf(line, "MemTotal: %ld", &mem_kb) == 1) {
      mem_kb = mem_kb * 4 / 5; // 80% of total as fallback
      break;
    }
  }
  fclose(f);
  if (mem_kb <= 0) return 2048;
  int mb = (int)((double)mem_kb / 1024 * 0.70);
  if (mb < 512) mb = 512;
  if (mb > 32768) mb = 32768;
  return mb;
}

// 从配置值中提取内存大小（MB），若为 auto 则重新检测并更新配置
int config_get_memory_mb(ConfigState *state) {
  const char *val = state->items[1].value;
  if (strncmp(val, "auto", 4) == 0) {
    int mb = detect_memory_mb();
    snprintf(state->items[1].value, sizeof(state->items[1].value), "auto(%d)",
             mb);
    return mb;
  }
  int mb = atoi(val);
  return mb > 0 ? mb : 2048;
}

void change_config(ConfigState *state) {

  int row, col;
  getmaxyx(stdscr, row, col);
  int idx = state->selected_item;

  if (idx == 1) {
    // ---- memory 配置：支持 "auto" 和手动输入数字 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[idx].key);
    mvprintw(5, 2, "Current config: %s", state->items[idx].value);
    mvprintw(9, 2, "Enter number or 'auto' (empty to cancel)");
    mvprintw(7, 2, "Enter new config: ");

    char input[64];
    getnstr(input, sizeof(input) - 1);
    if (strcmp(input, "") != 0) {
      if (strcmp(input, "auto") == 0) {
        int mb = detect_memory_mb();
        snprintf(state->items[idx].value,
                 sizeof(state->items[idx].value), "auto(%d)", mb);
      } else {
        // 验证是否为纯数字
        char *end;
        long val = strtol(input, &end, 10);
        if (*end == '\0' && val > 0) {
          snprintf(state->items[idx].value,
                   sizeof(state->items[idx].value), "%ld", val);
        }
        // 无效输入 → 不更新，保持原值
      }
      config_write(state);
    }
    noecho();
    curs_set(0);
    clear();

  } else if (idx == 4) {
    // ---- download_source：Official / BMCLAPI 选择器 ----
    const char *opts[] = {"Official", "BMCLAPI"};
    int sel = (strcmp(state->items[idx].value, "BMCLAPI") == 0) ? 1 : 0;
    while (1) {
      clear();
      mvprintw(3, 2, "Change config: %s", state->items[idx].key);
      mvprintw(5, 2, "Current: %s", state->items[idx].value);
      mvprintw(7, 2, "Select (j/k: switch, Enter: confirm, q: cancel):");
      for (int i = 0; i < 2; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw(9 + i, 4, "%s", opts[i]);
        if (i == sel) attroff(A_REVERSE);
      }
      int c = getch();
      if (c == 'j' || c == 'l') sel = 1;
      else if (c == 'k' || c == 'h') sel = 0;
      else if (c == '\n') {
        strcpy(state->items[idx].value, opts[sel]);
        config_write(state);
        break;
      } else if (c == 'q') break;
    }
    clear();

  } else if (idx == 5) {
    // ---- mod_source：CurseForge / Modrinth 选择器 ----
    const char *opts[] = {"CurseForge", "Modrinth"};
    int sel = (strcmp(state->items[idx].value, "Modrinth") == 0) ? 1 : 0;
    while (1) {
      clear();
      mvprintw(3, 2, "Change config: %s", state->items[idx].key);
      mvprintw(5, 2, "Current: %s", state->items[idx].value);
      mvprintw(7, 2, "Select (j/k: switch, Enter: confirm, q: cancel):");
      for (int i = 0; i < 2; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw(9 + i, 4, "%s", opts[i]);
        if (i == sel) attroff(A_REVERSE);
      }
      int c = getch();
      if (c == 'j' || c == 'l') sel = 1;
      else if (c == 'k' || c == 'h') sel = 0;
      else if (c == '\n') {
        strcpy(state->items[idx].value, opts[sel]);
        config_write(state);
        break;
      } else if (c == 'q') break;
    }
    clear();

  } else if (idx == 6) {
    // ---- threads：正整数输入 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[idx].key);
    mvprintw(5, 2, "Current config: %s", state->items[idx].value);
    mvprintw(9, 2, "Enter a number ≥1 (empty to cancel)");
    mvprintw(7, 2, "Enter new config: ");

    char input[64];
    getnstr(input, sizeof(input) - 1);
    if (strcmp(input, "") != 0) {
      char *end;
      long val = strtol(input, &end, 10);
      if (*end == '\0' && val >= 1) {
        snprintf(state->items[idx].value, sizeof(state->items[idx].value),
                 "%ld", val);
        config_write(state);
      }
    }
    noecho();
    curs_set(0);
    clear();

  } else if (idx < 4) {
    // ---- java_path (0), game_dir (2), jvm_args (3)：自由文本输入 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[idx].key);
    mvprintw(5, 2, "Current config: %s", state->items[idx].value);
    mvprintw(9, 2, "Tap nothing but enter to cancel");
    mvprintw(7, 2, "Enter new config: ");

    char input[64];
    getnstr(input, sizeof(input) - 1);
    if (strcmp(input, "") != 0) {
      strcpy(state->items[idx].value, input);
      config_write(state);
    }
    noecho();
    curs_set(0);
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
    // 如果重置的是 memory 且默认值为 auto，立即展开
    if (state->selected_item == 1 &&
        strcmp(state->items[1].value, "auto") == 0) {
      int mb = detect_memory_mb();
      snprintf(state->items[1].value, sizeof(state->items[1].value), "auto(%d)",
               mb);
    }
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
