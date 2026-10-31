#include "config.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <limits.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void reset_config(ConfigState *state);
void change_config(ConfigState *state);
void config_from_json(cJSON *json, ConfigState *state);
static int detect_memory_mb(void);
void remove_trailing_slash(char *str) {
  size_t len = strlen(str);
  if (len > 0 && str[len - 1] == '/') {
    str[len - 1] = '\0'; // 将 '/' 替换为字符串结束符
  }
}
void config_init(ConfigState *state) {
  // 初始化配置项
  char *home_dir = getenv("HOME");
  strcpy(state->home_dir, home_dir);
  state->item_count = 11;
  state->items = malloc(state->item_count * sizeof(ConfigItem));
  state->user_java_path_count = 0; // 用户添加的 java 路径数量

  // 打开tmcl目录
  char tmcl_dir[1024];
  char tmcl_config_path[1034];
  snprintf(tmcl_dir, sizeof(tmcl_dir), "%s/.tmcl", home_dir);
  snprintf(tmcl_config_path, sizeof(tmcl_config_path), "%s/tmcl.json",
           tmcl_dir);
  strcpy(state->tmcl_dir, tmcl_dir);
  strcpy(state->tmcl_config_path, tmcl_config_path);

  char game_dir[2048];
  snprintf(game_dir, sizeof(game_dir), "%s/.minecraft", home_dir);
  char *keys[] = {"java_path",      "memory",          "game_dir",
                  "jvm_args",       "download_cource", "mod_source",
                  "lib_threads",    "ast_threads",     "isolate",
                  "pinned_version", "last_play"};
  char *default_values[] = {"auto",     "auto",       game_dir, "",
                            "Official", "CurseForge", "16",     "96",
                            "yes",      "",           ""};

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
      for (int i = 0; i < state->item_count; i++) {
        cJSON *item = cJSON_GetObjectItem(root, keys[i]);
        if (item == NULL) {
          strcpy(state->items[i].value, default_values[i]);
          need_write = 1;
        } else {
          strcpy(state->items[i].value, item->valuestring);
        }
      }
      // 读取用户添加的 java 路径（隐藏配置项 java_paths，JSON 数组）
      cJSON *paths = cJSON_GetObjectItem(root, "java_paths");
      if (cJSON_IsArray(paths)) {
        cJSON *p;
        int n = 0;
        cJSON_ArrayForEach(p, paths) {
          if (n >= MAX_JAVA_PATHS)
            break;
          if (cJSON_IsString(p) && p->valuestring && p->valuestring[0])
            snprintf(state->user_java_paths[n++], JAVA_PATH_LEN, "%s",
                     p->valuestring);
        }
        state->user_java_path_count = n;
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

// 通过 key 字符串读写配置项，避免硬编码索引
const char *config_get(ConfigState *state, const char *key) {
  for (int i = 0; i < state->item_count; i++)
    if (strcmp(state->items[i].key, key) == 0)
      return state->items[i].value;
  return "";
}

void config_set(ConfigState *state, const char *key, const char *value) {
  for (int i = 0; i < state->item_count; i++)
    if (strcmp(state->items[i].key, key) == 0) {
      strncpy(state->items[i].value, value, sizeof(state->items[i].value) - 1);
      state->items[i].value[sizeof(state->items[i].value) - 1] = '\0';
      return;
    }
}

void config_page(int ch, int *middlep, ConfigState *state) {
  clear();
  int row = 0, col = 0;
  getmaxyx(stdscr, row, col);

  // 处理配置项选择
  switch (ch) {
  case 'j': // 向下选择
  case KEY_DOWN:
    if (state->selected_item < state->item_count - 3) { // 最后 2 项不可选
      state->selected_item++;
      int visible_rows = row - 10;
      if (state->selected_item >= state->scroll_offset + visible_rows) {
        state->scroll_offset++;
      }
    }
    break;
  case 'k': // 向上选择
  case KEY_UP:
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
    case 'h':
    case KEY_LEFT:
      *middlep = (*middlep + 1) % 2; // 循环向左（2个操作）
      break;
    case 'l':
    case KEY_RIGHT:
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
  if (!f)
    return 2048;
  char line[256];
  long mem_kb = 0;
  while (fgets(line, sizeof(line), f)) {
    if (sscanf(line, "MemAvailable: %ld", &mem_kb) == 1)
      break;
    if (sscanf(line, "MemTotal: %ld", &mem_kb) == 1) {
      mem_kb = mem_kb * 7 / 10; // 70% of total as fallback
      break;
    }
  }
  fclose(f);
  if (mem_kb <= 0)
    return 2048;
  int mb = (int)((double)mem_kb / 1024 * 0.70);
  if (mb < 512)
    mb = 512;
  if (mb > 32768)
    mb = 32768;
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

// 向列表追加一个 java 路径（realpath 解析软链接去重），成功返回 1
static int java_list_add(char list[][JAVA_PATH_LEN], int *count,
                         const char *path) {
  if (*count >= MAX_JAVA_PATHS)
    return 0;
  if (access(path, X_OK) != 0)
    return 0;
  // realpath 解析软链接（/usr/bin/java → 实际 jvm 路径），用于去重
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == NULL)
    return 0;
  for (int i = 0; i < *count; i++) {
    char old[PATH_MAX];
    if (realpath(list[i], old) != NULL && strcmp(old, resolved) == 0)
      return 0; // 已存在
  }
  snprintf(list[*count], JAVA_PATH_LEN, "%s", path);
  (*count)++;
  return 1;
}

// 自动检测系统中的 Java 路径（/usr/lib/jvm 目录扫描 + PATH），返回数量
int detect_java_paths(char list[][JAVA_PATH_LEN]) {
  int count = 0;

  // 1. 扫描 /usr/lib/jvm/*/bin/java（旧版 JRE 布局用 jre/bin/java）
  DIR *dir = opendir("/usr/lib/jvm");
  if (dir) {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.')
        continue;
      char cand[JAVA_PATH_LEN];
      snprintf(cand, sizeof(cand), "/usr/lib/jvm/%s/bin/java", ent->d_name);
      if (access(cand, X_OK) != 0)
        snprintf(cand, sizeof(cand), "/usr/lib/jvm/%s/jre/bin/java",
                 ent->d_name);
      java_list_add(list, &count, cand);
    }
    closedir(dir);
  }

  // 2. 扫描 PATH 各目录下的 java
  const char *path_env = getenv("PATH");
  if (path_env) {
    char *dup = strdup(path_env);
    char *saveptr;
    char *tok = strtok_r(dup, ":", &saveptr);
    while (tok) {
      char cand[JAVA_PATH_LEN];
      snprintf(cand, sizeof(cand), "%s/java", tok);
      java_list_add(list, &count, cand);
      tok = strtok_r(NULL, ":", &saveptr);
    }
    free(dup);
  }

  return count;
}

// 第 i 个可选项在列表序列中的位置（节标题各占 1 行）
static int java_seq_pos(int i, int det_count) {
  int pos = i;
  if (i >= 1)
    pos += 1; // "--- detected ---"
  if (i >= 1 + det_count)
    pos += 1; // "--- user added ---"
  return pos;
}

// ---- java_path 三栏选择器：auto / 自动检测 / 用户添加 ----
static void change_java_path(ConfigState *state) {
  int row, col;
  getmaxyx(stdscr, row, col);

  // 扫描系统 java 路径
  char detected[MAX_JAVA_PATHS][JAVA_PATH_LEN];
  int det_count = detect_java_paths(detected);
  int user_count = state->user_java_path_count;
  int total = 1 + det_count + user_count;

  // 初始光标定位到当前配置值所在行
  int sel = 0;
  const char *cur = state->items[0].value;
  if (strcmp(cur, "auto") != 0) {
    int found = 0;
    for (int i = 0; i < det_count && !found; i++)
      if (strcmp(detected[i], cur) == 0) {
        sel = 1 + i;
        found = 1;
      }
    for (int i = 0; i < user_count && !found; i++)
      if (strcmp(state->user_java_paths[i], cur) == 0) {
        sel = 1 + det_count + i;
        found = 1;
      }
  }
  int scroll_offset = 0;
  char msg[64] = ""; // 临时操作提示
  int msg_err = 0;   // 1 = 错误（红色显示）
  int action = 0;    // 底部操作栏光标：0=choose 1=new 2=delete

  int max_display = row - 5; // 列表窗口行数：base_y(4) 到 row-2
  if (max_display < 1)
    max_display = 1;

  while (1) {
    // 保证光标在窗口内
    if (java_seq_pos(sel, det_count) < java_seq_pos(scroll_offset, det_count)) {
      scroll_offset = sel;
    } else {
      while (java_seq_pos(sel, det_count) -
                 java_seq_pos(scroll_offset, det_count) >=
             max_display)
        scroll_offset++;
    }

    clear();
    // 右上角返回提示：与主页面 "tap [q] to quit" 同位置
    mvprintw(0, col - 15, "tap [q] to back");
    // 标题行：左标题 + 右当前值，与主页面表头同高、同起点，不再画下划线
    mvprintw(1, 3, "Java Path");
    mvprintw(1, 21, "Current: %s", state->items[0].value);

    int so_pos = java_seq_pos(scroll_offset, det_count);
    int base_y = 3;

    // 节标题（随列表滚动）
    int det_head = 1;
    int user_head = 2 + det_count;
    if (det_head >= so_pos)
      mvprintw(base_y + (det_head - so_pos), 3, "Detected%s",
               det_count ? "" : " (empty)");
    if (user_head >= so_pos)
      mvprintw(base_y + (user_head - so_pos), 3, "User added%s",
               user_count ? "" : " (empty)");

    // 可选项
    for (int i = scroll_offset; i < total; i++) {
      int y = base_y + (java_seq_pos(i, det_count) - so_pos);
      if (y >= row - 2)
        break;
      const char *label;
      if (i == 0)
        label = "auto";
      else if (i <= det_count)
        label = detected[i - 1];
      else
        label = state->user_java_paths[i - 1 - det_count];
      if (i == sel)
        attron(A_REVERSE);
      mvprintw(y, 5, "%s", label);
      if (i == sel)
        attroff(A_REVERSE);
    }

    // 滚动提示
    if (scroll_offset > 0)
      mvprintw(base_y - 1, 2, "...more above...");

    // 操作栏（最后一行，反色高亮当前操作），滚动提示放右下角
    {
      static const char *acts[3] = {"[c]hoose", "[n]ew", "[d]elete"};
      int cx = 0;
      for (int i = 0; i < 3; i++) {
        if (i == action)
          attron(A_REVERSE);
        mvprintw(row - 1, cx, "%s", acts[i]);
        if (i == action)
          attroff(A_REVERSE);
        cx += (int)strlen(acts[i]) + 1; // 操作之间隔一个空格
      }
    }
    if (java_seq_pos(total - 1, det_count) - so_pos >= max_display &&
        col > 16)
      mvprintw(row - 1, col - 15, "...more below...");
    // 提示行：倒数第二行、顶格，错误用红色（文案待本地化）
    if (msg[0]) {
      if (msg_err)
        attron(COLOR_PAIR(1));
      mvprintw(row - 2, 0, "%s", msg);
      if (msg_err)
        attroff(COLOR_PAIR(1));
    }
    refresh();

    int c = getch();
    msg[0] = '\0';
    msg_err = 0; // 每次输入先清空上一条提示的错误标记
    if (c == '\n')
      c = action == 0 ? 'c' : (action == 1 ? 'n' : 'd'); // Enter 执行光标所在操作
    if (c == 'j' || c == KEY_DOWN) {
      if (sel < total - 1)
        sel++;
    } else if (c == 'k' || c == KEY_UP) {
      if (sel > 0)
        sel--;
    } else if (c == 'c' || c == '\n') {
      // 选择并写入配置
      const char *label =
          sel == 0 ? "auto"
                   : (sel <= det_count
                          ? detected[sel - 1]
                          : state->user_java_paths[sel - 1 - det_count]);
      // 配置值缓冲区 128 字节，超长路径截断（精度限制避免编译警告）
      snprintf(state->items[0].value, sizeof(state->items[0].value), "%.*s",
               (int)sizeof(state->items[0].value) - 1, label);
      config_write(state);
      clear();
      return;
    } else if (c == 'n') {
      // 新增用户路径：输入行放在列表下方，避免盖住列表内容
      echo();
      curs_set(1);
      move(row - 2, 0);
      clrtoeol();
      mvprintw(row - 2, 0, "Enter new java path: ");
      refresh();
      char input[JAVA_PATH_LEN];
      getnstr(input, sizeof(input) - 1);
      noecho();
      curs_set(0);
      if (input[0] != '\0') {
        struct stat st;
        if (stat(input, &st) != 0 || !S_ISREG(st.st_mode) ||
            access(input, X_OK) != 0) {
          snprintf(msg, sizeof(msg),
                   "path does not exist or is not executable");
          msg_err = 1;
        } else {
          // realpath 去重（/usr/bin/java 与已检测路径可能指向同一二进制）
          int dup = 0;
          char in_resolved[PATH_MAX];
          if (realpath(input, in_resolved) != NULL) {
            for (int i = 0; i < user_count && !dup; i++) {
              char old[PATH_MAX];
              if (realpath(state->user_java_paths[i], old) != NULL &&
                  strcmp(old, in_resolved) == 0)
                dup = 1;
            }
            for (int i = 0; i < det_count && !dup; i++) {
              char old[PATH_MAX];
              if (realpath(detected[i], old) != NULL &&
                  strcmp(old, in_resolved) == 0)
                dup = 1;
            }
          }
          if (dup) {
            snprintf(msg, sizeof(msg), "path already in the list");
            msg_err = 1;
          } else if (user_count >= MAX_JAVA_PATHS) {
            snprintf(msg, sizeof(msg), "too many user paths");
            msg_err = 1;
          } else {
            snprintf(state->user_java_paths[user_count], JAVA_PATH_LEN, "%s",
                     input);
            state->user_java_path_count = user_count + 1;
            config_write(state);
            user_count = state->user_java_path_count;
            total = 1 + det_count + user_count;
            sel = 1 + det_count + user_count - 1;
            snprintf(msg, sizeof(msg), "added");
          }
        }
      }
    } else if (c == 'd') {
      // 仅用户添加的路径可删除
      if (sel > det_count && sel < total) {
        int uidx = sel - 1 - det_count;
        move(row - 2, 0);
        clrtoeol();
        mvprintw(row - 2, 0, "Delete %s? [y/N]", state->user_java_paths[uidx]);
        refresh();
        int c2 = getch();
        if (c2 == 'y' || c2 == 'Y') {
          // 整体前移（memmove 处理重叠区域）
          for (int i = uidx; i < user_count - 1; i++)
            memmove(state->user_java_paths[i], state->user_java_paths[i + 1],
                    JAVA_PATH_LEN);
          state->user_java_path_count = user_count - 1;
          config_write(state);
          user_count = state->user_java_path_count;
          total = 1 + det_count + user_count;
          if (sel >= total)
            sel = total - 1;
          snprintf(msg, sizeof(msg), "deleted");
        }
      } else {
        snprintf(msg, sizeof(msg), "only user-added paths can be deleted");
        msg_err = 1;
      }
    } else if (c == 'h' || c == KEY_LEFT) {
      action = (action + 2) % 3; // 操作栏光标左移
    } else if (c == 'l' || c == KEY_RIGHT) {
      action = (action + 1) % 3; // 操作栏光标右移
    } else if (c == 'q') {
      clear();
      return;
    }
  }
}

void change_config(ConfigState *state) {

  int idx = state->selected_item;

  const char *key = state->items[idx].key;

  if (strcmp(key, "java_path") == 0) {
    // ---- java_path：三栏选择器（auto / 自动检测 / 用户添加）----
    change_java_path(state);
  } else if (strcmp(key, "memory") == 0) {
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
        snprintf(state->items[idx].value, sizeof(state->items[idx].value),
                 "auto(%d)", mb);
      } else {
        // 验证是否为纯数字
        char *end;
        long val = strtol(input, &end, 10);
        if (*end == '\0' && val > 0) {
          snprintf(state->items[idx].value, sizeof(state->items[idx].value),
                   "%ld", val);
        }
        // 无效输入 → 不更新，保持原值
      }
      config_write(state);
    }
    noecho();
    curs_set(0);
    clear();

  } else if (strcmp(key, "download_cource") == 0) {
    // ---- download_cource：Official / BMCLAPI 选择器 ----
    const char *opts[] = {"Official", "BMCLAPI"};
    int sel = (strcmp(state->items[idx].value, "BMCLAPI") == 0) ? 1 : 0;
    while (1) {
      clear();
      mvprintw(3, 2, "Change config: %s", state->items[idx].key);
      mvprintw(5, 2, "Current: %s", state->items[idx].value);
      mvprintw(7, 2, "Select (j/k: switch, Enter: confirm, q: cancel):");
      for (int i = 0; i < 2; i++) {
        if (i == sel)
          attron(A_REVERSE);
        mvprintw(9 + i, 4, "%s", opts[i]);
        if (i == sel)
          attroff(A_REVERSE);
      }
      int c = getch();
      if (c == 'j' || c == 'l')
        sel = 1;
      else if (c == 'k' || c == 'h')
        sel = 0;
      else if (c == '\n') {
        strcpy(state->items[idx].value, opts[sel]);
        config_write(state);
        break;
      } else if (c == 'q')
        break;
    }
    clear();

  } else if (strcmp(key, "mod_source") == 0) {
    // ---- mod_source：CurseForge / Modrinth 选择器 ----
    const char *opts[] = {"CurseForge", "Modrinth"};
    int sel = (strcmp(state->items[idx].value, "Modrinth") == 0) ? 1 : 0;
    while (1) {
      clear();
      mvprintw(3, 2, "Change config: %s", state->items[idx].key);
      mvprintw(5, 2, "Current: %s", state->items[idx].value);
      mvprintw(7, 2, "Select (j/k: switch, Enter: confirm, q: cancel):");
      for (int i = 0; i < 2; i++) {
        if (i == sel)
          attron(A_REVERSE);
        mvprintw(9 + i, 4, "%s", opts[i]);
        if (i == sel)
          attroff(A_REVERSE);
      }
      int c = getch();
      if (c == 'j' || c == 'l')
        sel = 1;
      else if (c == 'k' || c == 'h')
        sel = 0;
      else if (c == '\n') {
        strcpy(state->items[idx].value, opts[sel]);
        config_write(state);
        break;
      } else if (c == 'q')
        break;
    }
    clear();

  } else if (strcmp(key, "lib_threads") == 0 ||
             strcmp(key, "ast_threads") == 0) {
    // ---- 下载线程数：正整数输入 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[idx].key);
    mvprintw(5, 2, "Current config: %s", state->items[idx].value);
    mvprintw(9, 2, "Enter a number >=1 (empty to cancel)");
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

  } else if (strcmp(key, "isolate") == 0) {
    // ---- isolate：yes / no 选择器 ----
    const char *opts[] = {"yes", "no"};
    int sel = (strcmp(state->items[idx].value, "no") == 0) ? 1 : 0;
    while (1) {
      clear();
      mvprintw(3, 2, "Change config: %s", state->items[idx].key);
      mvprintw(5, 2, "Current: %s", state->items[idx].value);
      mvprintw(7, 2, "yes = each version has its own saves/mods");
      mvprintw(8, 2, "no  = all versions share .minecraft");
      mvprintw(10, 2, "Select (j/k: switch, Enter: confirm, q: cancel):");
      for (int i = 0; i < 2; i++) {
        if (i == sel)
          attron(A_REVERSE);
        mvprintw(12 + i, 4, "%s", opts[i]);
        if (i == sel)
          attroff(A_REVERSE);
      }
      int c = getch();
      if (c == 'j' || c == 'l')
        sel = 1;
      else if (c == 'k' || c == 'h')
        sel = 0;
      else if (c == '\n') {
        strcpy(state->items[idx].value, opts[sel]);
        config_write(state);
        break;
      } else if (c == 'q')
        break;
    }
    clear();

  } else {
    // ---- game_dir (2), jvm_args (3)：自由文本输入 ----
    echo();
    curs_set(1);
    clear();
    mvprintw(3, 2, "Change config: %s", state->items[idx].key);
    mvprintw(5, 2, "Current config: %s", state->items[idx].value);
    mvprintw(9, 2, "Tap nothing but enter to cancel");
    mvprintw(7, 2, "Enter new config: ");

    char input[1024];
    getnstr(input, sizeof(input) - 1);
    if (strcmp(input, "") != 0) {
      if (strcmp(key, "game_dir") == 0)
        remove_trailing_slash("input");
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

  // 追加用户添加的 java 路径（隐藏配置项 java_paths）
  cJSON *paths = cJSON_CreateArray();
  for (int i = 0; i < state->user_java_path_count; i++)
    cJSON_AddItemToArray(paths, cJSON_CreateString(state->user_java_paths[i]));
  cJSON_AddItemToObject(root, "java_paths", paths);

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
