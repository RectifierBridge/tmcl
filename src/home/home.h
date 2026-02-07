#ifndef HOME_H
#define HOME_H

#include "../config/config.h"
#include "../version/version.h"

typedef struct {
  int pinned_index;
  int last_index;
  bool selected;
} HomeState;

// 函数声明
void home_init(HomeState *state, VersionState *VersionState,
               ConfigState *ConfigState);
void home_page(int ch, int *middlep, HomeState *state,
               VersionState *VersionState, ConfigState *ConfigState);

#endif
