#ifndef MOD_H
#define MOD_H

#include "../config/config.h"
#include "../version/version.h"

typedef struct {
  char **files;   // mods/ directory: all .jar files
  int *disabled;  // 1=disabled, 0=enabled
  int count;      // total file count
  int selected;   // cursor position (0-based)
  int action_sel; // action bar position (0=able, 1=new, 2=delete)
  int scroll;     // list scroll offset
  char version_name[256];
  char version[32];
  char version_dir[1024];
  char mods_dir[1024]; // actual mods path (considers isolate)
  char modloader[32];  // "forge", "fabric", "neoforge", "vanilla"
} ModState;

void mod_init(ModState *state, VersionState *VersionState);

void mod_page(ModState *state, VersionState *VersionState,
              ConfigState *ConfigState);

void mod_cleanup(ModState *state);

#endif
