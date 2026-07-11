#ifndef INSTALL_H
#define INSTALL_H

#include "../config/config.h"
#include "../version/version.h"
#include <ncurses.h>

// 安装 UI 状态
typedef struct {
  int selected_row;      // 当前选中的行 (0=type, 1=version, 2=name, 3=modloader, 4=install)
  int type_sel;          // type 子菜单的选中项
  int version_sel;       // version 列表的选中项
  int version_scroll;    // version 列表的滚动偏移
  int versions_count;    // 从 manifest 拉取的版本数量
  char **version_list;   // 版本 id 数组
  char **version_urls;   // 对应的版本 JSON URL 数组
} InstallState;

// 安装页面主函数
void install_page(VersionState *vstate, ConfigState *cstate);

// Forge/NeoForge 统一配置
typedef struct {
  const char *maven_base;    // "maven.minecraftforge.net" / "maven.neoforged.net"
  const char *group_id;      // "net.minecraftforge" / "net.neoforged"
  const char *artifact_id;   // "forge" / "neoforge"
  int has_promotions;        // forge=1, neoforge=0
} ForgeConfig;

// Forge/NeoForge 安装主函数（在 install.c 中实现）
// forge_version: 预先选定的版本号，非空时跳过版本选择 Phase 0
void do_forge_install(ConfigState *cstate, const char *mc_version,
                      const char *vanilla_url, const char *custom_name,
                      const char *modloader, const char *type,
                      const ForgeConfig *fcfg,
                      const char *forge_version);

// 旧版 Forge 安装主函数（1.7.10–1.12.2，launchwrapper 体系）
void do_legacy_forge_install(ConfigState *cstate, const char *mc_version,
                             const char *vanilla_url, const char *custom_name,
                             const char *modloader, const char *type,
                             const ForgeConfig *fcfg,
                             const char *forge_version);

#endif
