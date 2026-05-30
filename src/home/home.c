//  tmcl.c
#include "home.h"
#include "../version/version.h"
#include <cjson/cJSON.h>
#include <ncurses.h>
// #include <string.h>

void home_init(TmclState *state, VersionState *VersionState){
  state->selected = 0;
}

void home_page(int ch, int *middlep, TmclState *state, VersionState *VersionState){
  clear();

  //处理按键
  switch(ch){
  case 'j':
    state->selected = 1;
  case 'k':
    state->selected = 0;
    
  }
}
