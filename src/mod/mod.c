#define _GNU_SOURCE
// mod.c 模组列表管理
#include "mod.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- 从 tmcl_mods.json 读取禁用列表 ----
static void load_disabled_list(ModState *state) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/tmcl_mods.json", state->version_dir);
  FILE *f = fopen(path, "r");
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return;
  }
  fseek(f, 0, SEEK_SET);

  char *buf = malloc(sz + 1);
  fread(buf, 1, sz, f);
  buf[sz] = '\0';
  fclose(f);

  cJSON *root = cJSON_Parse(buf);
  free(buf);
  if (!root)
    return;

  cJSON *arr = cJSON_GetObjectItem(root, "disabled");
  if (arr && cJSON_IsArray(arr)) {
    int len = cJSON_GetArraySize(arr);
    for (int i = 0; i < len; i++) {
      cJSON *item = cJSON_GetArrayItem(arr, i);
      if (item && cJSON_IsString(item)) {
        const char *name = item->valuestring;
        for (int j = 0; j < state->count; j++) {
          if (strcmp(state->files[j], name) == 0) {
            state->disabled[j] = 1;
            break;
          }
        }
      }
    }
  }
  cJSON_Delete(root);
}

// ---- 保存禁用列表到 tmcl_mods.json ----
static void save_disabled_list(ModState *state) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/tmcl_mods.json", state->version_dir);

  cJSON *root = cJSON_CreateObject();
  cJSON *arr = cJSON_AddArrayToObject(root, "disabled");

  for (int i = 0; i < state->count; i++) {
    if (state->disabled[i]) {
      cJSON_AddItemToArray(arr, cJSON_CreateString(state->files[i]));
    }
  }

  char *json_str = cJSON_Print(root);
  FILE *f = fopen(path, "w");
  if (f) {
    fputs(json_str, f);
    fclose(f);
  }
  free(json_str);
  cJSON_Delete(root);
}

// ---- 初始化模组列表 ----
void mod_init(ModState *state, const char *version_name,
              const char *version_dir, const char *modloader, int isolated,
              const char *game_dir) {
  memset(state, 0, sizeof(*state));
  strncpy(state->version_name, version_name, sizeof(state->version_name) - 1);
  strncpy(state->version_dir, version_dir, sizeof(state->version_dir) - 1);
  strncpy(state->modloader, modloader, sizeof(state->modloader) - 1);

  // 确定 mods 目录
  if (isolated) {
    snprintf(state->version_dir, sizeof(state->version_dir), "%s/mods", version_dir);
  } else {
    snprintf(state->version_dir, sizeof(state->version_dir), "%s/mods", game_dir);
  }

  // 创建目录（如果不存在）
  mkdir(state->version_dir, 0755);

  // 扫描 .jar 文件
  DIR *dir = opendir(state->version_dir);
  if (!dir)
    return;

  // 先计数
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    {
      const char *ext = strrchr(entry->d_name, '.');
      if (ext && strcmp(ext, ".jar") == 0)
        state->count++;
    }
  }

  if (state->count == 0) {
    closedir(dir);
    return;
  }

  // 分配数组
  state->files = malloc(state->count * sizeof(char *));
  state->disabled = calloc(state->count, sizeof(int));

  rewinddir(dir);
  int idx = 0;
  while ((entry = readdir(dir)) != NULL) {
    const char *ext = strrchr(entry->d_name, '.');
    if (ext && strcmp(ext, ".jar") == 0) {
      state->files[idx++] = strdup(entry->d_name);
    }
  }
  closedir(dir);

  // 读取禁用列表
  load_disabled_list(state);
}

// 来自 version.h 的颜色映射
extern int modloader_color(const char *modloader, int selected);

// ---- 渲染模组列表 ----
static void render_mod_list(ModState *state, int row, int col) {
  (void)col;
  clear();

  // 标题: Mods for: <version_name> (<modloader>)
  int title_y = 1;
  attron(A_BOLD);
  mvprintw(title_y, 2, "Mods for: %s (", state->version_name);
  attroff(A_BOLD);

  // modloader 带色
  int mc = modloader_color(state->modloader, 0);
  if (mc)
    attron(mc);
  printw("%s", state->modloader);
  if (mc)
    attroff(mc);

  attron(A_BOLD);
  printw(")");
  attroff(A_BOLD);

  // 表头
  int head_y = title_y + 1;
  mvprintw(head_y, 2, "Able  Name");
  mvprintw(head_y + 1, 2, "----  ----");

  // 列表区域
  int list_y = head_y + 2;
  int max_display = row - list_y - 4;
  if (max_display < 1)
    max_display = 1;

  // 滚动调整
  if (state->selected < state->scroll)
    state->scroll = state->selected;
  if (state->selected >= state->scroll + max_display)
    state->scroll = state->selected - max_display + 1;

  if (state->count == 0) {
    attron(A_DIM);
    mvprintw(list_y, 4, "(no mods installed)");
    attroff(A_DIM);
  } else {
    for (int i = 0; i < max_display && (state->scroll + i) < state->count;
         i++) {
      int idx = state->scroll + i;
      int y = list_y + i;

      if (idx == state->selected)
        attron(A_REVERSE);

      // Able 列: * 或空格
      mvprintw(y, 2, "%c     ", state->disabled[idx] ? ' ' : '*');

      // Name 列
      printw("%s", state->files[idx]);

      if (idx == state->selected)
        attroff(A_REVERSE);
    }
  }

  // 底部操作栏
  const char *fmt[] = {"[a]ble", "[n]ew", "[d]elete"};
  int cx = 0;
  for (int i = 0; i < 3; i++) {
    if (i == state->action_sel)
      attron(A_REVERSE);
    mvprintw(row - 1, cx, "%s", fmt[i]);
    if (i == state->action_sel)
      attroff(A_REVERSE);
    cx += strlen(fmt[i]) + 1;
  }
}

// ---- 主页面循环 ----
void mod_page(ModState *state) {
  int row, col;
  getmaxyx(stdscr, row, col);

  while (1) {
    render_mod_list(state, row, col);
    refresh();

    int c = getch();

    // 列表导航: j/k/↓/↑
    if ((c == 'j' || c == KEY_DOWN) && state->count > 0 &&
        state->selected < state->count - 1)
      state->selected++;
    if ((c == 'k' || c == KEY_UP) && state->selected > 0)
      state->selected--;

    // 操作栏导航: h/←, l/→
    if (c == 'h' || c == KEY_LEFT)
      state->action_sel = (state->action_sel + 2) % 3;
    if (c == 'l' || c == KEY_RIGHT)
      state->action_sel = (state->action_sel + 1) % 3;

    // 直达操作键
    if (c == 'a')
      state->action_sel = 0;
    if (c == 'n')
      state->action_sel = 1;
    if (c == 'd')
      state->action_sel = 2;

    // 执行操作
    int action = -1;
    if (c == 'a')
      action = 0;
    if (c == 'n')
      action = 1;
    if (c == 'd')
      action = 2;
    if (c == '\n')
      action = state->action_sel;

    // --- Able: 切换启用/禁用 ---
    if (action == 0 && state->count > 0) {
      state->disabled[state->selected] = !state->disabled[state->selected];
      save_disabled_list(state);
    }

    // --- New: 占位 ---
    if (action == 1) {
      mvprintw(row - 1, 2,
               "Mod download not yet implemented. Press any key...");
      refresh();
      getch();
    }

    // --- Delete: 删除文件 + 更新 JSON ---
    if (action == 2 && state->count > 0) {
      int idx = state->selected;
      char filepath[1024];
      snprintf(filepath, sizeof(filepath), "%s/%s", state->version_dir,
               state->files[idx]);

      if (remove(filepath) == 0) {
        free(state->files[idx]);
        for (int i = idx; i < state->count - 1; i++) {
          state->files[i] = state->files[i + 1];
          state->disabled[i] = state->disabled[i + 1];
        }
        state->count--;
        if (state->selected >= state->count && state->count > 0)
          state->selected = state->count - 1;
        save_disabled_list(state);
      } else {
        mvprintw(row - 1, 2, "Failed to delete: %s. Press any key...",
                 state->files[idx]);
        refresh();
        getch();
      }
    }

    if (c == 'q')
      break;
  }
  clear();
}

// ---- 清理 ----
void mod_cleanup(ModState *state) {
  for (int i = 0; i < state->count; i++)
    free(state->files[i]);
  free(state->files);
  free(state->disabled);
  memset(state, 0, sizeof(*state));
}
