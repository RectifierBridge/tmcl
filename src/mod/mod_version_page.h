#ifndef MOD_VERSION_PAGE_H
#define MOD_VERSION_PAGE_H

#include "../config/config.h"
#include "mod.h"
#include "new_mod.h"
#include "mod_api.h"
#include <cjson/cJSON.h>

typedef struct {
  int selected;      // cursor position (0-based)
  int action_sel;    // action bar position (0=search)
  int scroll_offset; // list scroll offset
  int count_all;
  int count_flitered;
  char version_dir[1024];
  char mods_dir[1024];   // actual mods path (considers isolate)
  char install_as[1024]; // save as
  char modloader[32];    // mod loader ot filter
  char mc_version[64];   // minecraft version to filter（与 NewModState.mc_version 对齐）
  int action_count;
  cJSON *mod_version_root;   // 保留用于释放 cJSON 内存
  ModVersion *versions;      // 统一版本数组（替代 filtered_versions）
} ModVersionState;

void mod_version_page(ModVersionState *state, NewModState *NewModState);

#endif
