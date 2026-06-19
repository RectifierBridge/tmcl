// version.c
#include "version.h"
#include "../config/config.h"
#include "../account/account.h"
#include "install.h"
#include <cjson/cJSON.h>
#include <dirent.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 函数声明
char *classpath_from_json(VersionState *state, char *minecraft_dir);
void bottom_bar(int *middlep);

// 初始化版本数据
void init_versions(VersionState *state, ConfigState *ConfigState) {
  state->version_count = 0;
  state->selected_version = 0;
  state->scroll_offset = 0;

  char *gameDir = ConfigState->items[2].value;
  char versionDir[256];

  snprintf(versionDir, sizeof(versionDir), "%s/versions/", gameDir);

  DIR *dirp = opendir(versionDir);
  if (!dirp) {
    perror("opendir failed");
    return;
  }

  struct dirent *entry;
  state->versions = malloc(100 * sizeof(*(state->versions)));
  if (!state->versions) {
    closedir(dirp);
    return;
  }

  int count = 0;

  while ((entry = readdir(dirp)) != NULL && count < 100) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 &&
        entry->d_type == DT_DIR) {

      char versionPath[300]; // 增加缓冲区大小
      snprintf(versionPath, sizeof(versionPath), "%s%s/", versionDir,
               entry->d_name);

      DIR *child_dirp = opendir(versionPath);
      if (!child_dirp) {
        continue; // 无法打开子目录，跳过
      }

      char jsonName[150];
      char jarName[150];
      snprintf(jsonName, sizeof(jsonName), "%s.json", entry->d_name);
      snprintf(jarName, sizeof(jarName), "%s.jar", entry->d_name);

      int haveJar = 0, haveJson = 0;
      struct dirent *child_entry;

      while ((child_entry = readdir(child_dirp)) != NULL) {
        if (strcmp(child_entry->d_name, jarName) == 0) {
          haveJar = 1;
        }
        if (strcmp(child_entry->d_name, jsonName) == 0) {
          haveJson = 1;
        }
      }
      closedir(child_dirp); // 关闭子目录句柄

      if (haveJar && haveJson) {
        char json_path[350];
        snprintf(json_path, sizeof(json_path), "%s%s", versionPath, jsonName);

        FILE *json_file = fopen(json_path, "r");
        if (!json_file) {
          continue; // 无法打开文件，跳过
        }

        fseek(json_file, 0, SEEK_END);
        long json_file_size = ftell(json_file);
        fseek(json_file, 0, SEEK_SET);

        if (json_file_size <= 0) {
          fclose(json_file);
          continue;
        }

        char *json_data = malloc(json_file_size + 1);
        if (!json_data) {
          fclose(json_file);
          continue;
        }

        size_t read_size = fread(json_data, 1, json_file_size, json_file);
        json_data[read_size] = '\0';
        fclose(json_file);

        cJSON *json = cJSON_Parse(json_data);
        free(json_data);

        if (json) {
          cJSON *game_version = cJSON_GetObjectItem(json, "gameVersion");
          cJSON *game_type = cJSON_GetObjectItem(json, "type");
          cJSON *mod_loader_forge = cJSON_GetObjectItem(json, "forge");
          cJSON *mod_loader_fabric = cJSON_GetObjectItem(json, "fabric");

          if (game_type && game_type->valuestring) {
            // game type
            strncpy(state->versions[count].type, game_type->valuestring,
                    sizeof(state->versions[count].type) - 1);
            state->versions[count]
                .type[sizeof(state->versions[count].type) - 1] = '\0';

            // 从目录名获取版本名称
            strncpy(state->versions[count].name, entry->d_name,
                    sizeof(state->versions[count].name) - 1);
            state->versions[count]
                .name[sizeof(state->versions[count].name) - 1] = '\0';

            // game version number
            if (game_version && game_version->valuestring) {
              strncpy(state->versions[count].version, game_version->valuestring,
                      sizeof(state->versions[count].version) - 1);
              state->versions[count]
                  .version[sizeof(state->versions[count].version) - 1] = '\0';
            } else {
              strncpy(state->versions[count].version, "unknown",
                      sizeof(state->versions[count].version) - 1);
              state->versions[count]
                  .version[sizeof(state->versions[count].version) - 1] =
                  '\0'; // 补充缺失的终止符
            }

            // modloader 检查 fabric / forge
            if (mod_loader_fabric) {
              strncpy(state->versions[count].modloader, "fabric",
                      sizeof(state->versions[count].modloader) - 1);
              state->versions[count]
                  .modloader[sizeof(state->versions[count].modloader) - 1] =
                  '\0';
            } else if (mod_loader_forge) {
              strncpy(state->versions[count].modloader, "forge",
                      sizeof(state->versions[count].modloader) - 1);
              state->versions[count]
                  .modloader[sizeof(state->versions[count].modloader) - 1] =
                  '\0';
            } else {
              strncpy(state->versions[count].modloader, "vanilla",
                      sizeof(state->versions[count].modloader) - 1);
              state->versions[count]
                  .modloader[sizeof(state->versions[count].modloader) - 1] =
                  '\0'; // 补充缺失的终止符
            }

            // 提取 mainClass
            cJSON *main_class = cJSON_GetObjectItem(json, "mainClass");
            if (main_class && main_class->valuestring) {
              strncpy(state->versions[count].mainClass, main_class->valuestring,
                      sizeof(state->versions[count].mainClass) - 1);
              state->versions[count]
                  .mainClass[sizeof(state->versions[count].mainClass) - 1] =
                  '\0';
            } else {
              strncpy(state->versions[count].mainClass,
                      "net.minecraft.client.main.Main",
                      sizeof(state->versions[count].mainClass) - 1);
              state->versions[count]
                  .mainClass[sizeof(state->versions[count].mainClass) - 1] =
                  '\0';
            }

            // 提取 inheritsFrom
            cJSON *inherits = cJSON_GetObjectItem(json, "inheritsFrom");
            if (inherits && inherits->valuestring) {
              strncpy(state->versions[count].inheritsFrom,
                      inherits->valuestring,
                      sizeof(state->versions[count].inheritsFrom) - 1);
              state->versions[count]
                  .inheritsFrom[sizeof(state->versions[count].inheritsFrom) -
                                1] = '\0';
            } else {
              state->versions[count].inheritsFrom[0] = '\0';
            }

            // 提取 javaVersion.majorVersion
            state->versions[count].javaMajor = 0;
            cJSON *java_ver = cJSON_GetObjectItem(json, "javaVersion");
            if (java_ver) {
              cJSON *major = cJSON_GetObjectItem(java_ver, "majorVersion");
              if (major && cJSON_IsNumber(major)) {
                state->versions[count].javaMajor = major->valueint;
              }
            }

            count++;
          }
          cJSON_Delete(json); // 释放 cJSON 对象
        }
      }
    }
  }
  closedir(dirp);

  // 排序（仅当count>1时）
  if (count > 1) {
    for (int i = 0; i < count - 1; i++) {
      for (int j = i + 1; j < count; j++) {
        if (strcmp(state->versions[i].name, state->versions[j].name) > 0) {
          VersionInfo temp = state->versions[i];
          state->versions[i] = state->versions[j];
          state->versions[j] = temp;
        }
      }
    }
  }

  state->version_count = count;
}

// ======================== 启动辅助函数 ========================

// 替换参数占位符 ${...}
static void sub_arg(char *dest, const char *src, size_t dest_size,
                    const char *vname, const char *gdir, const char *adir,
                    const char *aidx, const char *ndir, const char *cp,
                    const char *uname, const char *uuid, const char *tok,
                    const char *utype, const char *vtype) {
  dest[0] = '\0';
  size_t di = 0;
  const char *p = src;

  while (*p && di < dest_size - 1) {
    if (*p == '$' && *(p + 1) == '{') {
      const char *end = strchr(p + 2, '}');
      if (end) {
        size_t plen = end - p - 2;
        char key[64] = {0};
        if (plen < sizeof(key))
          memcpy(key, p + 2, plen);

        const char *val = NULL;
        if (strcmp(key, "auth_player_name") == 0)      val = uname;
        else if (strcmp(key, "version_name") == 0)     val = vname;
        else if (strcmp(key, "game_directory") == 0)   val = gdir;
        else if (strcmp(key, "assets_root") == 0)      val = adir;
        else if (strcmp(key, "assets_index_name") == 0) val = aidx;
        else if (strcmp(key, "auth_uuid") == 0)        val = uuid;
        else if (strcmp(key, "auth_access_token") == 0) val = tok;
        else if (strcmp(key, "user_type") == 0)        val = utype;
        else if (strcmp(key, "version_type") == 0)     val = vtype;
        else if (strcmp(key, "natives_directory") == 0) val = ndir;
        else if (strcmp(key, "launcher_name") == 0)    val = "tmcl";
        else if (strcmp(key, "launcher_version") == 0) val = "0.0.1";
        else if (strcmp(key, "classpath") == 0)        val = cp;
        else if (strcmp(key, "clientid") == 0)         val = "0";
        else if (strcmp(key, "auth_xuid") == 0)        val = "0";
        else if (strcmp(key, "user_properties") == 0) val = "{}";

        if (val) {
          size_t rlen = strlen(val);
          if (di + rlen < dest_size) {
            memcpy(dest + di, val, rlen);
            di += rlen;
            dest[di] = '\0';
          }
        }
        p = end + 1;
        continue;
      }
    }
    dest[di++] = *p++;
    dest[di] = '\0';
  }
}

// 检查 rules 数组是否允许当前平台 (linux)
static int rules_ok(cJSON *rules) {
  if (!rules || !cJSON_IsArray(rules)) return 1; // 无规则则允许
  int allowed = 0;
  cJSON *rule;
  cJSON_ArrayForEach(rule, rules) {
    cJSON *action = cJSON_GetObjectItem(rule, "action");
    cJSON *os = cJSON_GetObjectItem(rule, "os");
    cJSON *features = cJSON_GetObjectItem(rule, "features");

    // 无 os 且无 features → 全局规则
    if (!os && !features) {
      if (action && strcmp(action->valuestring, "disallow") == 0) allowed = 0;
      if (action && strcmp(action->valuestring, "allow") == 0) allowed = 1;
      continue;
    }

    if (os) {
      cJSON *os_name = cJSON_GetObjectItem(os, "name");
      if (os_name && strcmp(os_name->valuestring, "linux") == 0) {
        if (action && strcmp(action->valuestring, "allow") == 0) allowed = 1;
        if (action && strcmp(action->valuestring, "disallow") == 0) allowed = 0;
      }
    }
  }
  return allowed;
}

// 读取单个版本的 JSON 文件
static cJSON *read_ver_json(const char *mcdir, const char *vname) {
  char path[512];
  snprintf(path, sizeof(path), "%s/versions/%s/%s.json", mcdir, vname, vname);
  FILE *f = fopen(path, "r");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = malloc(sz + 1);
  if (!data) { fclose(f); return NULL; }
  fread(data, 1, sz, f);
  data[sz] = '\0';
  fclose(f);
  cJSON *json = cJSON_Parse(data);
  free(data);
  return json;
}

// 将一个 arguments 数组（jvm 或 game）展开追加到 args 列表
static void append_args(cJSON *arr, char **args, int *ac, int max_ac,
                        char ***tofree, int *fc, const char *vname,
                        const char *gdir, const char *adir, const char *aidx,
                        const char *ndir, const char *cp, const char *uname,
                        const char *uuid, const char *tok, const char *utype,
                        const char *vtype) {
  if (!arr || !cJSON_IsArray(arr)) return;
  cJSON *it;
  cJSON_ArrayForEach(it, arr) {
    if (*ac >= max_ac - 1) break;

    if (cJSON_IsString(it)) {
      char *buf = malloc(1024);
      sub_arg(buf, it->valuestring, 1024, vname, gdir, adir, aidx, ndir, cp,
              uname, uuid, tok, utype, vtype);
      args[(*ac)++] = buf;
      (*tofree)[(*fc)++] = buf;
    } else if (cJSON_IsObject(it)) {
      cJSON *rules = cJSON_GetObjectItem(it, "rules");
      if (!rules_ok(rules)) continue;
      cJSON *val = cJSON_GetObjectItem(it, "value");
      if (!val) continue;
      if (cJSON_IsString(val)) {
        char *buf = malloc(1024);
        sub_arg(buf, val->valuestring, 1024, vname, gdir, adir, aidx, ndir, cp,
                uname, uuid, tok, utype, vtype);
        args[(*ac)++] = buf;
        (*tofree)[(*fc)++] = buf;
      } else if (cJSON_IsArray(val)) {
        cJSON *vi;
        cJSON_ArrayForEach(vi, val) {
          if (*ac >= max_ac - 1) break;
          if (cJSON_IsString(vi)) {
            char *buf = malloc(1024);
            sub_arg(buf, vi->valuestring, 1024, vname, gdir, adir, aidx, ndir,
                    cp, uname, uuid, tok, utype, vtype);
            args[(*ac)++] = buf;
            (*tofree)[(*fc)++] = buf;
          }
        }
      }
    }
  }
}

// 处理旧版 minecraftArguments 字符串（按空格拆分）
static void legacy_args(const char *mc_args, char **args, int *ac, int max_ac,
                        char ***tofree, int *fc, const char *vname,
                        const char *gdir, const char *adir, const char *aidx,
                        const char *ndir, const char *cp, const char *uname,
                        const char *uuid, const char *tok, const char *utype,
                        const char *vtype) {
  if (!mc_args) return;
  char *dup = strdup(mc_args);
  char *saveptr;
  char *token = strtok_r(dup, " ", &saveptr);
  while (token && *ac < max_ac - 1) {
    char *buf = malloc(1024);
    sub_arg(buf, token, 1024, vname, gdir, adir, aidx, ndir, cp, uname, uuid,
            tok, utype, vtype);
    args[(*ac)++] = buf;
    (*tofree)[(*fc)++] = buf;
    token = strtok_r(NULL, " ", &saveptr);
  }
  free(dup);
}

// 根据 majorVersion 查找对应的 Java 可执行文件
static int find_java_for_version(int major, char *out, size_t out_size) {
  // 首先尝试常见路径
  const char *candidates[] = {
      "/usr/lib/jvm/java-%d-openjdk/bin/java",
      "/usr/lib/jvm/jdk-%d/bin/java",
      "/usr/lib/jvm/java-%d/bin/java",
      "/usr/lib/jvm/jre-%d/bin/java",
      "/usr/lib/jvm/java-%d-openjdk/jre/bin/java",
      NULL,
  };
  for (int i = 0; candidates[i]; i++) {
    char path[512];
    snprintf(path, sizeof(path), candidates[i], major);
    if (access(path, X_OK) == 0) {
      strncpy(out, path, out_size - 1);
      out[out_size - 1] = '\0';
      return 1;
    }
  }
  return 0;
}

// ======================== 启动版本函数 ========================
void begin_version(VersionState *state, ConfigState *ConfigState) {
  int idx = state->selected_version;
  if (state->version_count == 0 || idx < 0 || idx >= state->version_count)
    return;

  strcpy(ConfigState->items[7].value, state->versions[idx].name);
  config_write(ConfigState);

  endwin();
  char *mcdir = ConfigState->items[2].value;
  const char *vname = state->versions[idx].name;
  const char *inh = state->versions[idx].inheritsFrom;

  // 构建 classpath（含继承链）
  char classpath[16384];
  strcpy(classpath, classpath_from_json(state, mcdir));

  // 读取版本 JSON（优先用子版本，其次父版本的信息）
  cJSON *child_json = read_ver_json(mcdir, vname);
  cJSON *parent_json = (inh && strlen(inh) > 0) ? read_ver_json(mcdir, inh)
                                                 : NULL;

  // ---- 决定 mainClass ----
  char main_class[128];
  strncpy(main_class, state->versions[idx].mainClass, sizeof(main_class) - 1);
  main_class[sizeof(main_class) - 1] = '\0';

  // ---- 决定 assetIndex ----
  char asset_index[32] = "legacy";
  cJSON *asset_idx = NULL;
  if (child_json)
    asset_idx = cJSON_GetObjectItem(child_json, "assetIndex");
  if (!asset_idx && parent_json)
    asset_idx = cJSON_GetObjectItem(parent_json, "assetIndex");
  if (asset_idx) {
    cJSON *aid = cJSON_GetObjectItem(asset_idx, "id");
    if (aid && aid->valuestring) {
      strncpy(asset_index, aid->valuestring, sizeof(asset_index) - 1);
      asset_index[sizeof(asset_index) - 1] = '\0';
    }
  }

  // ---- 找 natives 目录 ----
  char natives_dir[512] = "";
  char vdir[512];
  snprintf(vdir, sizeof(vdir), "%s/versions/%s", mcdir, vname);
  DIR *ndp = opendir(vdir);
  if (ndp) {
    struct dirent *dent;
    while ((dent = readdir(ndp)) != NULL) {
      if (strncmp(dent->d_name, "natives", 7) == 0 &&
          dent->d_type == DT_DIR) {
        snprintf(natives_dir, sizeof(natives_dir), "%s/versions/%s/%s", mcdir,
                 vname, dent->d_name);
        break;
      }
    }
    closedir(ndp);
  }
  // 回退到默认 natives 路径
  if (natives_dir[0] == '\0') {
    snprintf(natives_dir, sizeof(natives_dir), "%s/versions/%s/natives", mcdir,
             vname);
  }

  // ---- 路径 ----
  char assets_dir[256];
  snprintf(assets_dir, sizeof(assets_dir), "%s/assets", mcdir);

  // ---- 账户信息（从选中的账户读取）----
  AccountInfo *acct = g_account_state ? account_get_selected(g_account_state) : NULL;
  const char *uname = acct ? acct->username : "Player";
  const char *uuid_str = acct ? acct->uuid : "00000000-0000-0000-0000-000000000000";
  const char *token_str = (acct && acct->access_token[0]) ? acct->access_token : "0";
  // userType: microsoft → msa, 其他 → mojang
  const char *utype_str = (acct && strcmp(acct->type, "microsoft") == 0) ? "msa" : "mojang";
  const char *vtype_str = state->versions[idx].type;

  // ---- 离线皮肤（CustomSkinLoader）----
  char *skin_arg = NULL;
  if (acct && strcmp(acct->type, "offline") == 0 && acct->skin_path[0]) {
    asprintf(&skin_arg, "-Dcustomskinloader.skinPath=%s", acct->skin_path);
  }

  // ---- authlib-injector（LittleSkin / 自定义 Yggdrasil 皮肤）----
  char *injector_arg = NULL;
  if (acct && (strcmp(acct->type, "littleskin") == 0 ||
               strcmp(acct->type, "third_party") == 0)) {
    const char *server = acct->auth_server;
    if (!server || !server[0])
      server = "https://littleskin.cn/api/yggdrasil";
    else if (strcmp(acct->type, "littleskin") == 0 &&
             strstr(server, "/authserver") != NULL)
      server = "https://littleskin.cn/api/yggdrasil"; // 修复旧存储
    char injector_path[600];
    // 使用 home_dir 路径
    snprintf(injector_path, sizeof(injector_path), "%s/.tmcl/authlib-injector.jar",
             ConfigState->home_dir);
    // 检查是否已有有效的 authlib-injector.jar
    int need_dl = 1;
    if (access(injector_path, F_OK) == 0) {
      FILE *test = fopen(injector_path, "rb");
      if (test) {
        unsigned char magic[2];
        if (fread(magic, 1, 2, test) == 2 &&
            magic[0] == 'P' && magic[1] == 'K') {
          need_dl = 0; // 有效 jar
        }
        fclose(test);
      }
      if (need_dl) remove(injector_path);
    }
    if (need_dl) {
      printf("Downloading authlib-injector...\n");
      // 先通过 API 获取真实下载 URL
      char url_buf[512] = {0};
      FILE *fp = popen(
          "curl -sL 'https://authlib-injector.yushi.moe/artifact/latest.json'",
          "r");
      if (fp) {
        fread(url_buf, 1, sizeof(url_buf) - 1, fp);
        pclose(fp);
        cJSON *latest = cJSON_Parse(url_buf);
        if (latest) {
          cJSON *dl = cJSON_GetObjectItem(latest, "download_url");
          if (dl && cJSON_IsString(dl)) {
            char dcmd[1280];
            snprintf(dcmd, sizeof(dcmd), "curl -s -L -o \"%s\" \"%s\"",
                     injector_path, dl->valuestring);
            system(dcmd);
          }
          cJSON_Delete(latest);
        }
      }
    }
    if (access(injector_path, F_OK) == 0) {
      asprintf(&injector_arg, "-javaagent:%s=%s", injector_path, server);
    }
  }

  // ---- 内存（auto 时自动刷新）----
  char mem_arg[80];
  int mem_mb = config_get_memory_mb(ConfigState);
  snprintf(mem_arg, sizeof(mem_arg), "-Xmx%dm", mem_mb);

  // ---- 构建 args ----
  char *args[300];
  int ac = 0;
  char **tofree = malloc(300 * sizeof(char *));
  int fc = 0;

  // 使用版本匹配的 Java（如果有指定 majorVersion）
  int jmajor = state->versions[idx].javaMajor;
  char java_bin[512];
  if (jmajor > 0 && find_java_for_version(jmajor, java_bin, sizeof(java_bin))) {
    args[ac++] = strdup(java_bin);
    tofree[fc++] = args[ac - 1];
    printf("  Java: %s (required major version: %d)\n", java_bin, jmajor);
  } else {
    args[ac++] = ConfigState->items[0].value; // 使用配置的默认 java
  }
  args[ac++] = mem_arg;

  // ---- JVM 参数 ----
  cJSON *args_obj = NULL;
  if (child_json)
    args_obj = cJSON_GetObjectItem(child_json, "arguments");
  if (!args_obj && parent_json)
    args_obj = cJSON_GetObjectItem(parent_json, "arguments");

  if (args_obj) {
    // 新版 arguments 格式
    cJSON *jvm = cJSON_GetObjectItem(args_obj, "jvm");
    append_args(jvm, args, &ac, 300, &tofree, &fc, vname, vdir, assets_dir,
                asset_index, natives_dir, classpath, uname, uuid_str,
                token_str, utype_str, vtype_str);

    // authlib-injector / 离线皮肤（必须在 -cp 之前）
    if (skin_arg && ac < 300 - 4) {
      args[ac++] = skin_arg;
      tofree[fc++] = skin_arg;
    }
    if (injector_arg && ac < 300 - 4) {
      args[ac++] = injector_arg;
      tofree[fc++] = injector_arg;
      args[ac++] = "-Dauthlibinjector.side=client";
    }

    // 确保 -cp 已在 JVM 参数末尾（JSON 中通常有，但也做一次兜底）
    if (ac < 300 - 4) {
      args[ac++] = "-cp";
      args[ac++] = classpath;
    }

    // 添加主类
    args[ac++] = main_class;

    // 游戏参数
    cJSON *game = cJSON_GetObjectItem(args_obj, "game");
    append_args(game, args, &ac, 300, &tofree, &fc, vname, vdir, assets_dir,
                asset_index, natives_dir, classpath, uname, uuid_str,
                token_str, utype_str, vtype_str);
  } else {
    // 旧版格式：使用 minecraftArguments
    const char *mc_args = NULL;
    if (child_json)
      mc_args =
          cJSON_GetStringValue(cJSON_GetObjectItem(child_json,
                                                    "minecraftArguments"));
    if (!mc_args && parent_json)
      mc_args =
          cJSON_GetStringValue(cJSON_GetObjectItem(parent_json,
                                                    "minecraftArguments"));

    // 旧版 JVM 参数
    // 离线皮肤 / authlib-injector
    if (skin_arg && ac < 300 - 4) {
      args[ac++] = skin_arg;
      tofree[fc++] = skin_arg;
    }
    if (injector_arg && ac < 300 - 4) {
      args[ac++] = injector_arg;
      tofree[fc++] = injector_arg;
      args[ac++] = "-Dauthlibinjector.side=client";
    }

    args[ac++] = "-Djava.library.path=";
    args[ac - 1] = malloc(1024);
    tofree[fc++] = args[ac - 1];
    snprintf(args[ac - 1], 1024, "-Djava.library.path=%s", natives_dir);

    args[ac++] = "-cp";
    args[ac++] = classpath;
    args[ac++] = main_class;

    if (mc_args) {
      legacy_args(mc_args, args, &ac, 300, &tofree, &fc, vname, vdir,
                  assets_dir, asset_index, natives_dir, classpath, uname,
                  uuid_str, token_str, utype_str, vtype_str);
    } else {
      // 最简回退
      args[ac++] = "--username";
      args[ac++] = (char *)uname; // const cast safe for exec
      args[ac++] = "--version";
      args[ac++] = (char *)vname;
      args[ac++] = "--gameDir";
      args[ac++] = vdir;
      args[ac++] = "--assetsDir";
      args[ac++] = assets_dir;
      args[ac++] = "--assetIndex";
      args[ac++] = asset_index;
      args[ac++] = "--accessToken";
      args[ac++] = (char *)token_str;
      args[ac++] = "--uuid";
      args[ac++] = (char *)uuid_str;
      args[ac++] = "--userType";
      args[ac++] = (char *)utype_str;
    }
  }

  args[ac] = NULL;

  // ---- 清理 JSON ----
  if (child_json) cJSON_Delete(child_json);
  if (parent_json) cJSON_Delete(parent_json);

  // ---- 输出信息 ----
  printf("Launching %s (%s)\n", vname, state->versions[idx].modloader);
  printf("  Main class: %s\n", main_class);
  printf("  Asset index: %s\n", asset_index);
  printf("  Natives dir: %s\n", natives_dir);
  printf("  Classpath length: %zu\n", strlen(classpath));

  // ---- 执行 ----
  if (chdir(vdir) != 0) {
    perror("chdir failed");
  }

  pid_t pid = fork();
  if (pid == 0) {
    execvp(args[0], args);
    perror("execvp failed");
    exit(1);
  } else if (pid > 0) {
    printf("Minecraft 启动中 (PID: %d)...\n", pid);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
      printf("Minecraft 已退出，退出码: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Minecraft 被信号终止: %d\n", WTERMSIG(status));
    }
  } else {
    perror("fork failed");
  }

  // ---- 释放动态分配的参数 ----
  for (int i = 0; i < fc; i++) {
    free(tofree[i]);
  }
  free(tofree);
}

// pin_version函数
void pin_version(VersionState *state, ConfigState *ConfigState) {
  strcpy(ConfigState->items[6].value,
         state->versions[state->selected_version].name);
  config_write(ConfigState);
}

// mod函数
void mod(VersionState *state, int index) {
  if (index < 0 || index >= state->version_count)
    return;
}
// 重命名版本函数
void rename_version(VersionState *state, int index) {
  if (index < 0 || index >= state->version_count || state->version_count == 0)
    return;

  int row, col;
  getmaxyx(stdscr, row, col);

  echo(); // 开启回显
  curs_set(1);
  clear();

  mvprintw(3, 2, "Rename version: %s", state->versions[index].version);
  mvprintw(5, 2, "Current name: %s", state->versions[index].name);
  mvprintw(7, 2, "Enter new name: ");

  char new_name[64];
  getnstr(new_name, sizeof(new_name) - 1);

  strncpy(state->versions[index].name, new_name,
          sizeof(state->versions[index].name) - 1);

  noecho(); // 关闭回显
  curs_set(0);

  // 清除重命名界面，主循环会重新绘制完整界面
  clear();
}

// 新建版本函数
void new_version(VersionState *state, ConfigState *ConfigState) {
  install_page(state, ConfigState);
  // 刷新版本列表
  init_versions(state, ConfigState);
}

// 删除版本函数
void delete_version(VersionState *state, int index, ConfigState *ConfigState) {
  if (index < 0 || index >= state->version_count) {
    return;
  }

  echo();      // 开启回显
  curs_set(1); // 显示光标
  clear();

  mvprintw(3, 2, "delete version: %s", state->versions[index].name);
  mvprintw(5, 2, "Do you want to delete[y/N]");
  char c = getch();
  char delete_command[1020];
  switch (c) {
  case 'y':
    snprintf(delete_command, sizeof(delete_command), "rm -rf %s/versions/%s",
             ConfigState->items[2].value, state->versions[index].name);
    system(delete_command);
    break;
  default:
    break;
  }
  noecho(); // 关闭回显
  curs_set(0);

  init_versions(state, ConfigState);

  // 清除重命名界面，主循环会重新绘制完整界面
  clear();
  return;
}

// 版本页面主函数
void version_page(int ch, int *middlep, VersionState *state,
                  ConfigState *ConfigState) {
  clear();
  int row = 0, col = 0;
  getmaxyx(stdscr, row, col);

  // 处理版本选择导航
  switch (ch) {
  case 'j': // 向下选择版本
    if (state->selected_version < state->version_count - 1) {
      state->selected_version++;
      int visible_rows = row - 10;
      if (state->selected_version >= state->scroll_offset + visible_rows) {
        state->scroll_offset++;
      }
    }
    break;
  case 'k': // 向上选择版本
    if (state->selected_version > 0) {
      state->selected_version--;
      if (state->selected_version < state->scroll_offset) {
        state->scroll_offset--;
      }
    }
    break;
  case 'b': // begin 操作 - 直接执行
    begin_version(state, ConfigState);
    *middlep = 0; // 移动光标到 start
    break;
  case 'm':
    *middlep = 1;
    mod(state, state->selected_version);
    break;
  case 'p':
    *middlep = 2;
    pin_version(state, ConfigState);
    break;
  case 'r': // rename 操作 - 直接执行
    rename_version(state, state->selected_version);
    *middlep = 3; // 移动光标到 rename
    break;
  case 'n': // new 操作 - 直接执行
    new_version(state, ConfigState);
    *middlep = 4; // 移动光标到 new
    break;
  case 'd': // delete 操作 - 直接执行
    delete_version(state, state->selected_version, ConfigState);
    *middlep = 5; // 移动光标到 delete
    break;
  case 'h':                        // 在操作间向左移动
    *middlep = (*middlep + 5) % 6; // 循环向左（5个操作）
    break;
  case 'l':                        // 在操作间向右移动
    *middlep = (*middlep + 1) % 6; // 循环向右（5个操作）
    break;
  case '\n': // 回车键执行当前操作
    switch (*middlep) {
    case 0: // begin
      begin_version(state, ConfigState);
      break;
    case 1: // mod
      mod(state, state->selected_version);
      break;
    case 2: // pin
      pin_version(state, ConfigState);
      break;
    case 3: // rename
      rename_version(state, state->selected_version);
      break;
    case 4: // new
      new_version(state, ConfigState);
      break;
    case 5: // delete
      delete_version(state, state->selected_version, ConfigState);
      break;
    }
    break;
  }

  // 显示版本列表标题
  mvprintw(2, 3, "Type    Modloader Version   Name");
  mvprintw(3, 3, "----    --------- -------   ----");

  // 显示版本列表
  int start_y = 4;                     // 列表开始的行
  int max_display = row - start_y - 4; // 最大显示数量

  for (int i = state->scroll_offset;
       i < state->version_count && i < state->scroll_offset + max_display;
       i++) {

    // 移动到正确的位置
    move(start_y + i - state->scroll_offset, 1);

    // 如果是选中的版本，高亮显示
    if (i == state->selected_version) {
      attron(A_REVERSE);
    }

    char pin[4] = " ";
    if (strcmp(ConfigState->items[6].value, state->versions[i].name) == 0) {
      strcpy(pin, "*");
    }
    // 显示版本信息：pin否、类型、模组加载器、版本号、名字
    printw("%s %-7s %-9s %-9s %s", pin, state->versions[i].type,
           state->versions[i].modloader, state->versions[i].version,
           state->versions[i].name);

    if (i == state->selected_version) {
      attroff(A_REVERSE);
    }
  }

  // 显示滚动提示
  if (state->scroll_offset > 0) {
    mvprintw(start_y - 1, 2, "...more above...");
  }
  if (state->scroll_offset + max_display < state->version_count) {
    mvprintw(start_y + max_display, 2, "...more below...");
  }

  // 底部操作提示
  bottom_bar(middlep);
}

// 辅助函数：将某个版本的 JSON 中的 libraries 追加到 classpath
void append_json_libraries(const char *minecraft_dir, const char *version_name,
                           char *classpath, size_t classpath_size) {
  char json_path[512];
  snprintf(json_path, sizeof(json_path), "%s/versions/%s/%s.json",
           minecraft_dir, version_name, version_name);

  FILE *file = fopen(json_path, "r");
  if (!file) {
    return;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *json_data = malloc(file_size + 1);
  if (!json_data) {
    fclose(file);
    return;
  }

  fread(json_data, 1, file_size, file);
  json_data[file_size] = '\0';
  fclose(file);

  cJSON *root = cJSON_Parse(json_data);
  if (!root) {
    free(json_data);
    return;
  }

  cJSON *libraries = cJSON_GetObjectItem(root, "libraries");
  if (!libraries) {
    cJSON_Delete(root);
    free(json_data);
    return;
  }

  cJSON *library;
  cJSON_ArrayForEach(library, libraries) {
    // 检查rules（如果有的话）
    cJSON *rules = cJSON_GetObjectItem(library, "rules");
    if (rules) {
      cJSON *rule = cJSON_GetArrayItem(rules, 0);
      if (rule) {
        cJSON *action = cJSON_GetObjectItem(rule, "action");
        cJSON *os = cJSON_GetObjectItem(rule, "os");
        if (action && os) {
          cJSON *os_name = cJSON_GetObjectItem(os, "name");
          if (os_name && strcmp(cJSON_GetStringValue(os_name), "linux") != 0) {
            continue;
          }
        }
      }
    }

    // 获取库下载信息
    cJSON *downloads = cJSON_GetObjectItem(library, "downloads");
    if (downloads) {
      cJSON *artifact = cJSON_GetObjectItem(downloads, "artifact");
      if (artifact) {
        cJSON *path = cJSON_GetObjectItem(artifact, "path");
        if (path) {
          char lib_path[512];
          snprintf(lib_path, sizeof(lib_path), "%s/libraries/%s", minecraft_dir,
                   cJSON_GetStringValue(path));

          size_t current_len = strlen(classpath);
          if (current_len > 0 &&
              current_len + strlen(lib_path) + 2 < classpath_size) {
            strcat(classpath, ":");
          }
          strcat(classpath, lib_path);
        }
      }
    } else {
      // 如果没有downloads字段，尝试从name字段构建路径
      cJSON *name = cJSON_GetObjectItem(library, "name");
      if (name) {
        char *name_str = strdup(cJSON_GetStringValue(name));
        char *saveptr;
        char *group = strtok_r(name_str, ":", &saveptr);
        char *artifact = strtok_r(NULL, ":", &saveptr);
        char *version = strtok_r(NULL, ":", &saveptr);

        if (group && artifact && version) {
          // 将 group 中的 '.' 替换为 '/'（Maven 路径格式）
          char group_path[256];
          snprintf(group_path, sizeof(group_path), "%s", group);
          for (char *gp = group_path; *gp; gp++) {
            if (*gp == '.') *gp = '/';
          }

          char lib_path[512];
          snprintf(lib_path, sizeof(lib_path),
                   "%s/libraries/%s/%s/%s/%s-%s.jar", minecraft_dir, group_path,
                   artifact, version, artifact, version);

          size_t current_len = strlen(classpath);
          if (current_len > 0 &&
              current_len + strlen(lib_path) + 2 < classpath_size) {
            strcat(classpath, ":");
          }
          strcat(classpath, lib_path);
        }
        free(name_str);
      }
    }
  }

  cJSON_Delete(root);
  free(json_data);
}

// 从json读取并构建classpath（支持继承链）
char *classpath_from_json(VersionState *state, char *minecraft_dir) {
  int index = state->selected_version;
  static char classpath[16384];
  char version_dir[256];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", minecraft_dir,
           state->versions[index].name);

  classpath[0] = '\0';

  // 1) 首先附加上父版本（inheritsFrom）的 libraries 和 jar
  if (strlen(state->versions[index].inheritsFrom) > 0) {
    append_json_libraries(minecraft_dir,
                          state->versions[index].inheritsFrom, classpath,
                          sizeof(classpath));

    // 同时添加父版本的 jar（Fabric / 新版 Forge 需要）
    if (strlen(classpath) > 0) {
      strcat(classpath, ":");
    }
    char parent_jar[512];
    snprintf(parent_jar, sizeof(parent_jar), "%s/versions/%s/%s.jar",
             minecraft_dir, state->versions[index].inheritsFrom,
             state->versions[index].inheritsFrom);
    strcat(classpath, parent_jar);
  }

  // 2) 然后附加当前版本的 libraries
  append_json_libraries(minecraft_dir, state->versions[index].name, classpath,
                        sizeof(classpath));

  // 3) 最后添加主游戏 jar 文件
  if (strlen(classpath) > 0) {
    strcat(classpath, ":");
  }
  strcat(classpath, version_dir);
  strcat(classpath, "/");
  strcat(classpath, state->versions[index].name);
  strcat(classpath, ".jar");

  return classpath;
}

// bottom bar
void bottom_bar(int *middlep) {
  int row, col;
  getmaxyx(stdscr, row, col);

  switch (*middlep) {
  case 0:
    attron(A_REVERSE);
    mvprintw(row - 1, 0, "[b]egin");
    attroff(A_REVERSE);
    printw(" [m]od [p]in [r]ename [n]ew [d]elete");
    break;
  case 1:
    mvprintw(row - 1, 0, "[b]egin ");
    attron(A_REVERSE);
    printw("[m]od");
    attroff(A_REVERSE);
    printw(" [p]in [r]ename [n]ew [d]elete");
    break;
  case 2:
    mvprintw(row - 1, 0, "[b]egin [m]od ");
    attron(A_REVERSE);
    printw("[p]in");
    attroff(A_REVERSE);
    printw(" [r]ename [n]ew [d]elete");
    break;
  case 3:
    mvprintw(row - 1, 0, "[b]egin [m]od [p]in ");
    attron(A_REVERSE);
    printw("[r]ename");
    attroff(A_REVERSE);
    printw(" [n]ew [d]elete");
    break;
  case 4:
    mvprintw(row - 1, 0, "[b]egin [m]od [p]in [r]ename ");
    attron(A_REVERSE);
    printw("[n]ew");
    attroff(A_REVERSE);
    printw(" [d]elete");
    break;
  case 5:
    mvprintw(row - 1, 0, "[b]egin [m]od [p]in [r]ename [n]ew ");
    attron(A_REVERSE);
    printw("[d]elete");
    attroff(A_REVERSE);
    break;
  }
}

// 清理版本数据
void free_versions(VersionState *state) {
  if (state->versions) {
    free(state->versions);
    state->versions = NULL;
  }
  state->version_count = 0;
}
