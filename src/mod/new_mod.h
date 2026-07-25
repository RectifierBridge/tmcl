#ifndef NEW_MOD_H
#define NEW_MOD_H

#include "../config/config.h"
#include "mod.h"

typedef struct {
  int selected;      // cursor position (0-based)
  int action_sel;    // action bar position (0=search)
  int scroll_offset; // list scroll offset
  int count;
  char version_name[256];
  char version_dir[1024];
  char mods_dir[1024]; // actual mods path (considers isolate)
  char modloader[32];  // mod loader of the version
  char mc_version[32]; // minecraft version
  char loader[32];     // mod loader of the mod you want to search
  char order[32];      // the order of the search result
  char search[1024];   // the key word you search
  char source[32];
  int action_count;
  char mod_id[32];
  char mod_title[128];
} NewModState;

void new_mod_init(NewModState *state, ModState *ModState, char *mod_source);

void new_mod_page(NewModState *state, ConfigState *ConfigState);

void new_mod_cleanup(NewModState *state);

void new_mod_page(NewModState *state, ConfigState *ConfigState);

#endif
