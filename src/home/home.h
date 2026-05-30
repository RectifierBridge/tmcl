#ifndef HOME_H
#define HOME_H

#include "../version/version.h"

typedef struct {
  bool selected;
} TmclState;

//函数声明
void home_page(int ch, int *middlep, TmclState *state, VersionState *VersionState);
#endif
