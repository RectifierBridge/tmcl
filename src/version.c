#include "version.h"
#include "config.h"
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

char *classpath_from_json(VersionState *state, char *minecraft_dir);

// 名称排序
// int compare_strings(const void *a, const void *b) {
//   return strcmp(*(const char **)a, *(const char **)b);
// }

// 初始化版本数据
void init_versions(VersionState *state, ConfigState *ConfigState) {
    state->version_count = 0;
    state->selected_version = 0;
    state->scroll_offset = 0;
    
    char *gameDir = ConfigState->items[2].value;
    char versionDir[256];
    
    // 使用snprintf避免缓冲区溢出
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
            
            char versionPath[300];  // 增加缓冲区大小
            snprintf(versionPath, sizeof(versionPath), "%s%s/", versionDir, entry->d_name);
            
            DIR *child_dirp = opendir(versionPath);
            if (!child_dirp) {
                continue;  // 无法打开子目录，跳过
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
            closedir(child_dirp);  // 关闭子目录句柄
            
            if (haveJar && haveJson) {
                char json_path[350];
                snprintf(json_path, sizeof(json_path), "%s%s", versionPath, jsonName);
                
                FILE *json_file = fopen(json_path, "r");
                if (!json_file) {
                    continue;  // 无法打开文件，跳过
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
                    
                    if (game_type && game_type->valuestring) {
                        // 使用安全复制，避免溢出
                        strncpy(state->versions[count].type, game_type->valuestring, 
                                sizeof(state->versions[count].type) - 1);
                        state->versions[count].type[sizeof(state->versions[count].type) - 1] = '\0';
                        
                        strncpy(state->versions[count].name, entry->d_name,
                                sizeof(state->versions[count].name) - 1);
                        state->versions[count].name[sizeof(state->versions[count].name) - 1] = '\0';
                        
                        if (game_version && game_version->valuestring) {
                            strncpy(state->versions[count].version, game_version->valuestring,
                                    sizeof(state->versions[count].version) - 1);
                            state->versions[count].version[sizeof(state->versions[count].version) - 1] = '\0';
                        } else {
                            strncpy(state->versions[count].version, "unknown",
                                    sizeof(state->versions[count].version) - 1);
                        }
                        
                        state->versions[count].modloader[0] = '\0';
                        count++;
                    }
                    cJSON_Delete(json);  // 释放cJSON对象
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

// 启动版本函数
void begin_version(VersionState *state, ConfigState *ConfigState) {
  endwin();
  int index = state->selected_version;
  char *minecraft_dir = ConfigState->items[2].value;
  char version_dir[256]; // .../versions/[version]
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", minecraft_dir,
           state->versions[index].name);
  char *java_path = ConfigState->items[0].value;

  // 准备参数
  char *args[100];
  int arg_count = 0;

  char max_memory[64];
  char max_memory_arg[70];
  strcpy(max_memory, ConfigState->items[1].value);
  snprintf(max_memory_arg, sizeof(max_memory_arg), "-Xmx%sm", max_memory);

  char assets_path[164];
  snprintf(assets_path, sizeof(assets_path), "%s/assets", minecraft_dir);

  char *classpath = classpath_from_json(state, minecraft_dir);

  args[arg_count++] = java_path;
  args[arg_count++] = max_memory_arg;
  args[arg_count++] = "-Djava.library.path=/home/xy/.minecraft/versions/"
                      "1.21.10/natives-linux-x86_64";
  args[arg_count++] = "-cp";
  args[arg_count++] = classpath;
  args[arg_count++] = "net.minecraft.client.main.Main";
  args[arg_count++] = "--username";
  args[arg_count++] = "rectifier";
  args[arg_count++] = "--version";
  args[arg_count++] = "1.21.10";
  args[arg_count++] = "--gameDir";
  args[arg_count++] = minecraft_dir;
  args[arg_count++] = "--assetsDir";
  args[arg_count++] = assets_path;
  args[arg_count++] = "--assetIndex";
  args[arg_count++] = "27";
  args[arg_count++] = "--accessToken";
  args[arg_count++] = "offline_token";
  args[arg_count++] = "--uuid";
  args[arg_count++] = "00000000-0000-0000-0000-000000000000";
  args[arg_count++] = "--userType";
  args[arg_count++] = "mojang";
  args[arg_count] = NULL; // 参数列表必须以NULL结尾

  // 切换到Minecraft目录
  if (chdir(minecraft_dir) != 0) {
    perror("chdir failed");
  }

  // 创建子进程执行游戏
  pid_t pid = fork();
  if (pid == 0) {
    // 子进程
    execvp(java_path, args);
    // 如果execvp返回，说明出错了
    perror("execvp failed");
    exit(1);
  } else if (pid > 0) {
    // 父进程
    printf("Minecraft启动中 (PID: %d)...\n", pid);

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      printf("Minecraft已退出，退出码: %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Minecraft被信号终止: %d\n", WTERMSIG(status));
    }

  } else {
    perror("fork failed");
  }
}

// mod函数
void mod(VersionState *state, int index) {
  if (index < 0 || index >= state->version_count)
    return;
}
// 重命名版本函数
void rename_version(VersionState *state, int index) {
  if (index < 0 || index >= state->version_count)
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
void new_version(VersionState *state) {
  // 这里可以添加新建版本的逻辑
  // 例如：弹出对话框让用户输入版本信息
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
  case 'r': // rename 操作 - 直接执行
    rename_version(state, state->selected_version);
    *middlep = 2; // 移动光标到 rename
    break;
  case 'n': // new 操作 - 直接执行
    new_version(state);
    *middlep = 3; // 移动光标到 new
    break;
  case 'd': // delete 操作 - 直接执行
    delete_version(state, state->selected_version, ConfigState);
    *middlep = 4; // 移动光标到 delete
    break;
  case 'h':                        // 在操作间向左移动
    *middlep = (*middlep + 4) % 5; // 循环向左（5个操作）
    break;
  case 'l':                        // 在操作间向右移动
    *middlep = (*middlep + 1) % 5; // 循环向右（5个操作）
    break;
  case '\n': // 回车键执行当前操作
    switch (*middlep) {
    case 0: // begin
      begin_version(state, ConfigState);
      break;
    case 1: // mod
      mod(state, state->selected_version);
      break;
    case 2: // rename
      rename_version(state, state->selected_version);
      break;
    case 3: // new
      new_version(state);
      break;
    case 4: // delete
      delete_version(state, state->selected_version, ConfigState);
      break;
    }
    break;
  }

  // 显示版本列表标题
  mvprintw(2, 2, "Type    Modloader Version   Name");
  mvprintw(3, 2, "----    --------- -------   ----");

  // 显示版本列表
  int start_y = 4;                     // 列表开始的行
  int max_display = row - start_y - 4; // 最大显示数量

  for (int i = state->scroll_offset;
       i < state->version_count && i < state->scroll_offset + max_display;
       i++) {

    // 移动到正确的位置
    move(start_y + i - state->scroll_offset, 2);

    // 如果是选中的版本，高亮显示
    if (i == state->selected_version) {
      attron(A_REVERSE);
    }

    // 如果是选定版本，标记星号
    // char chosen_mark = (i == state->chosen_version) ? '*' : ' ';

    // 显示版本信息：类型、模组加载器、版本号、名字
    printw("%-7s %-9s %-9s %s", state->versions[i].type,
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
  switch (*middlep) {
  case 0:
    attron(A_REVERSE);
    mvprintw(row - 1, 0, "[b]egin");
    attroff(A_REVERSE);
    printw(" [m]od [r]ename [n]ew [d]elete");
    break;
  case 1:
    mvprintw(row - 1, 0, "[b]egin ");
    attron(A_REVERSE);
    printw("[m]od");
    attroff(A_REVERSE);
    printw(" [r]ename [n]ew [d]elete");
    break;
  case 2:
    mvprintw(row - 1, 0, "[b]egin [m]od ");
    attron(A_REVERSE);
    printw("[r]ename");
    attroff(A_REVERSE);
    printw(" [n]ew [d]elete");
    break;
  case 3:
    mvprintw(row - 1, 0, "[b]egin [m]od [r]ename ");
    attron(A_REVERSE);
    printw("[n]ew");
    attroff(A_REVERSE);
    printw(" [d]elete");
    break;
  case 4:
    mvprintw(row - 1, 0, "[b]egin [m]od [r]ename [n]ew ");
    attron(A_REVERSE);
    printw("[d]elete");
    attroff(A_REVERSE);
    break;
  }

  // 页面标题和导航
  move(0, 0);
  attron(A_REVERSE);
  printw("[V]ersion");
  attroff(A_REVERSE);
  printw(" [C]onfig [A]ccount");
  mvprintw(0, col - 15, "tap [q] to quit");
}



// 从json读取并构建classpath
char *classpath_from_json(VersionState *state, char *minecraft_dir) {
  int index = state->selected_version;
  char json_path[512];
  static char classpath[10240];
  char version_dir[256]; // .../versions/[version]
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", minecraft_dir,
           state->versions[index].name);

  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           state->versions[index].name);

  // 读取json文件
  FILE *file = fopen(json_path, "r");
  if (!file) {
    perror("无法打开json文件");
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *json_data = malloc(file_size + 1);
  if (!json_data) {
    fclose(file);
    return NULL;
  }

  fread(json_data, 1, file_size, file);
  json_data[file_size] = '\0';
  fclose(file);

  // 解析JSON
  cJSON *root = cJSON_Parse(json_data);
  if (!root) {
    printf("JSON解析失败: %s\n", cJSON_GetErrorPtr());
    free(json_data);
    return NULL;
  }

  // 获取libraries数组
  cJSON *libraries = cJSON_GetObjectItem(root, "libraries");
  if (!libraries) {
    printf("找不到libraries字段\n");
    cJSON_Delete(root);
    free(json_data);
    return NULL;
  }

  // 构建classpath
  classpath[0] = '\0';

  // 首先添加所有库文件
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
            continue; // 跳过非linux平台的库
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

          // 检查库文件是否存在
          if (strlen(classpath) > 0) {
            strcat(classpath, ":");
          }
          strcat(classpath, lib_path);
        }
      }
    } else {
      // 如果没有downloads字段，尝试从name字段构建路径
      cJSON *name = cJSON_GetObjectItem(library, "name");
      if (name) {
        // 解析Maven格式的name: group:artifact:version
        char *name_str = strdup(cJSON_GetStringValue(name));
        char *saveptr;
        char *group = strtok_r(name_str, ":", &saveptr);
        char *artifact = strtok_r(NULL, ":", &saveptr);
        char *version = strtok_r(NULL, ":", &saveptr);

        if (group && artifact && version) {
          // 构建Maven路径
          char lib_path[512];
          snprintf(lib_path, sizeof(lib_path),
                   "%s/libraries/%s/%s/%s/%s-%s.jar", minecraft_dir, group,
                   artifact, version, artifact, version);

          if (strlen(classpath) > 0) {
            strcat(classpath, ":");
          }
          strcat(classpath, lib_path);
        }
        free(name_str);
      }
    }
  }
  if (strcmp(classpath, "") != 0) {
    // 添加主游戏jar文件
    strcat(classpath, ":");
    strcat(classpath, version_dir);
    strcat(classpath, "/");
    strcat(classpath, state->versions[index].name);
    strcat(classpath, ".jar");
  }
  return classpath;
}

// 清理版本数据
void free_versions(VersionState *state) {
  if (state->versions) {
    free(state->versions);
    state->versions = NULL;
  }
  state->version_count = 0;
}
