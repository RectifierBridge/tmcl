#ifndef HOME_H
#define HOME_H

#include "../version/version.h"

typedef struct {
  bool selected;
} HomeState;

// 函数声明
void home_init(HomeState *state, VersionState *VersionState);
void home_page(int ch, int *middlep, HomeState *state,
               VersionState *VersionState);

#endif
