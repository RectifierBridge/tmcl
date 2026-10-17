#define _GNU_SOURCE
// mod.c — 模组列表管理
#include "mod.h"
#include "../version/version.h"
#include "new_mod.h"
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 来自 version.h 的颜色映射
extern int modloader_color(const char *modloader, int selected);

// ---- 初始化模组列表 ----
void mod_init(ModState *state, VersionState *VersionState) {
  memset(state, 0, sizeof(*state));
  bool isolated =
      VersionState->versions[VersionState->selected_version].isolate;
  snprintf(state->version_name, sizeof(state->version_name), "%s",
           VersionState->versions[VersionState->selected_version].name);
  // selected_version_dir 声明 2048，实际内容 <300，按 version_dir 容量截断
  snprintf(state->version_dir, sizeof(state->version_dir), "%.*s",
           (int)sizeof(state->version_dir) - 1,
           VersionState->selected_version_dir);
  snprintf(state->modloader, sizeof(state->modloader), "%s",
           VersionState->versions[VersionState->selected_version].modloader);
  snprintf(state->version, sizeof(state->version), "%s",
           VersionState->versions[VersionState->selected_version].version);

  // 确定 mods 目录（path + "/mods" 按 mods_dir 容量截断，实际内容远小于容量）
  if (isolated) {
    snprintf(state->mods_dir, sizeof(state->mods_dir), "%.*s/mods",
             (int)sizeof(state->mods_dir) - 6, state->version_dir);
  } else {
    snprintf(state->mods_dir, sizeof(state->mods_dir), "%.*s/mods",
             (int)sizeof(state->mods_dir) - 6, VersionState->game_dir);
  }

  // 创建目录（如果不存在）
  mkdir(state->mods_dir, 0755);

  // 扫描 .jar 和 .jar.disabled 文件
  DIR *dir = opendir(state->mods_dir);
  if (!dir)
    return;

  // 先计数
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    size_t nlen = strlen(name);
    if (nlen > 4 && strcmp(name + nlen - 4, ".jar") == 0)
      state->count++;
    else if (nlen > 13 && strcmp(name + nlen - 13, ".jar.disabled") == 0)
      state->count++;
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
    const char *name = entry->d_name;
    size_t nlen = strlen(name);
    if (nlen > 13 && strcmp(name + nlen - 13, ".jar.disabled") == 0) {
      // 去掉 .disabled 后缀得到显示名
      size_t dlen = nlen - 9; // len(".disabled") = 9
      state->files[idx] = malloc(dlen + 1);
      memcpy(state->files[idx], name, dlen);
      state->files[idx][dlen] = '\0';
      state->disabled[idx] = 1;
      idx++;
    } else if (nlen > 4 && strcmp(name + nlen - 4, ".jar") == 0) {
      state->files[idx] = strdup(name);
      state->disabled[idx] = 0;
      idx++;
    }
  }
  closedir(dir);
}

// ---- 渲染模组列表 ----
static void render_mod_list(ModState *state, int row, int col) {
  (void)col;
  clear();

  // 标题: Mods for: <version_name> (<modloader>)
  mvprintw(0, 0, "Mods for: ");
  attron(A_BOLD);
  printw("%s", state->version_name);
  attroff(A_BOLD);

  // modloader 带色
  printw(" (");
  int mc = modloader_color(state->modloader, 0);
  if (mc)
    attron(COLOR_PAIR(mc));
  printw("%s", state->modloader);
  if (mc)
    attroff(COLOR_PAIR(mc));
  printw(")");

  // 表头
  int head_y = 2;
  mvprintw(head_y, 3, "Able  Name");
  mvprintw(head_y + 1, 3, "----  ----");

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
      mvprintw(y, 3, "%c     ", state->disabled[idx] ? ' ' : '*');

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
void mod_page(ModState *state, VersionState *VersionState,
              ConfigState *ConfigState) {
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

    // --- Able: 切换启用/禁用 (文件重命名) ---
    if (action == 0 && state->count > 0) {
      int idx = state->selected;
      char oldpath[1040], newpath[1040]; // mods_dir(1023) + "/" + ".disabled"(9) + NUL = 1034
      if (state->disabled[idx]) {
        // 启用: foo.jar.disabled → foo.jar
        snprintf(oldpath, sizeof(oldpath), "%s/%s.disabled", state->mods_dir,
                 state->files[idx]);
        snprintf(newpath, sizeof(newpath), "%s/%s", state->mods_dir,
                 state->files[idx]);
      } else {
        // 禁用: foo.jar → foo.jar.disabled
        snprintf(oldpath, sizeof(oldpath), "%s/%s", state->mods_dir,
                 state->files[idx]);
        snprintf(newpath, sizeof(newpath), "%s/%s.disabled", state->mods_dir,
                 state->files[idx]);
      }
      if (rename(oldpath, newpath) == 0)
        state->disabled[idx] = !state->disabled[idx];
    }

    // --- New: 占位 ---
    if (action == 1) {
      NewModState NewModState = {0};
      new_mod_init(&NewModState, state, ConfigState->items[5].value);
      new_mod_page(&NewModState, ConfigState);
      mod_init(state, VersionState);
    }

    // --- Delete: 删除文件 ---
    if (action == 2 && state->count > 0) {
      int idx = state->selected;
      char filepath[1040]; // mods_dir(1023) + "/" + ".disabled"(9) + NUL = 1034
      // 尝试删除 .jar 或 .jar.disabled
      snprintf(filepath, sizeof(filepath), "%s/%s", state->mods_dir,
               state->files[idx]);
      if (remove(filepath) != 0) {
        snprintf(filepath, sizeof(filepath), "%s/%s.disabled", state->mods_dir,
                 state->files[idx]);
        remove(filepath);
      }

      free(state->files[idx]);
      for (int i = idx; i < state->count - 1; i++) {
        state->files[i] = state->files[i + 1];
        state->disabled[i] = state->disabled[i + 1];
      }
      state->count--;
      if (state->selected >= state->count && state->count > 0)
        state->selected = state->count - 1;
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
