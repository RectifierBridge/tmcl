#ifndef VERSION_H
#define VERSION_H

#include "../config/config.h"
#include <ncurses.h>

// 版本信息结构
typedef struct {
  char version[32];     // 版本号，如 "1.20.1"
  char type[16];        // 类型，如 "release", "snapshot"
  char modloader[32];   // 模组加载器，如 "vanilla", "fabric", "forge"
  char name[64];        // 自定义名字
  char mainClass[128];  // 主类，如 "net.minecraft.client.main.Main" 或
                        // "cpw.mods.modlauncher.Launcher"
  char inheritsFrom[64]; // 继承的父版本名（forge/fabric 常用）
  int javaMajor;         // 所需的 Java 主版本号，0 表示使用默认
} VersionInfo;

// 版本页面状态
typedef struct {
  VersionInfo *versions;
  int version_count;
  int selected_version; // 当前选中的版本索引
  int scroll_offset;    // 列表滚动偏移
} VersionState;

// 函数声明
void init_versions(VersionState *state, ConfigState *ConfigState);
void version_page(int ch, int *middlep, VersionState *state,
                  ConfigState *ConfigState);
void free_versions(VersionState *state);
void rename_version(VersionState *state, int index);
void choose_version(VersionState *state, int index);
void start_version(VersionState *state, int index);
void new_version(VersionState *state);
void delete_version(VersionState *state, int index, ConfigState *ConfigState);
void begin_version(VersionState *state, ConfigState *ConfigState);

#endif
