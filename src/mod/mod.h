#ifndef MOD_H
#define MOD_H

typedef struct {
  char **files;          // mods/ directory: all .jar files
  int   *disabled;       // 1=disabled, 0=enabled
  int    count;          // total file count
  int    selected;       // cursor position (0-based)
  int    action_sel;     // action bar position (0=able, 1=new, 2=delete)
  int    scroll;         // list scroll offset
  char   version_name[256];
  char   version_dir[1024];
  char   mods_dir[1024]; // actual mods path (considers isolate)
  char   modloader[32];  // "forge", "fabric", "neoforge", "vanilla"
} ModState;

void mod_init(ModState *state, const char *version_name,
              const char *version_dir, const char *modloader,
              int isolated, const char *game_dir);

void mod_page(ModState *state);

void mod_cleanup(ModState *state);

#endif
