# Forge/NeoForge Installation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Forge 1.13+ and NeoForge installation in TMCL using pure manual approach — download libraries, execute install-profile processors, and generate self-contained version JSON.

**Architecture:** All new code goes into `src/version/install.c` and `src/version/install.h`. A unified `ForgeConfig` struct differentiates Forge vs NeoForge (Maven repo, group/artifact IDs). The main orchestrator `do_forge_install()` reuses existing `dl_parallel()`, `retry_failed()`, `build_lib_task()`, and `lib_rules_ok()`. Six-phase pipeline: fetch maven metadata → user picks version → download installer jar + extract install_profile.json → download all libraries (vanilla + forge) → execute processors sequentially → merge JSON + download assets.

**Tech Stack:** C, ncurses, cJSON, curl (via fork/exec), unzip (via popen/popen), pthread

## Global Constraints

- Forge 1.13+ only (modlauncher/installer profile system); not 1.7.10–1.12.2
- NeoForge 1.20.1+ (same installer structure, different Maven repo)
- Self-contained version JSON (no `inheritsFrom`, like Fabric)
- Processors execute sequentially (output of one is input to next)
- All `malloc` must have corresponding `free`; use `snprintf` + `sizeof` for buffers
- `g_cancel` flag checked during processor execution for SIGINT support
- HTTP forced to HTTPS in all download URLs

---

### Task 1: Add ForgeConfig struct to install.h

**Files:**
- Modify: `src/version/install.h:1-22`

**Interfaces:**
- Produces: `ForgeConfig` struct, `do_forge_install()` declaration

- [ ] **Step 1: Add ForgeConfig struct and do_forge_install declaration**

Add after `InstallState` struct, before `#endif`:

```c
// Forge/NeoForge 统一配置
typedef struct {
  const char *maven_base;    // "maven.minecraftforge.net" / "maven.neoforged.net"
  const char *group_id;      // "net.minecraftforge" / "net.neoforged"
  const char *artifact_id;   // "forge" / "neoforge"
  int has_promotions;        // forge=1, neoforge=0
} ForgeConfig;

// Forge/NeoForge 安装主函数（在 install.c 中实现）
void do_forge_install(struct ConfigState *cstate, const char *mc_version,
                      const char *vanilla_url, const char *custom_name,
                      const char *modloader, const char *type,
                      const ForgeConfig *fcfg);
```

- [ ] **Step 2: Verify compilation**

```bash
make clean && make
```
(Will fail, or produce unused-function warnings; that's OK — the function isn't defined yet.)

---

### Task 2: Replace "Coming soon!" with do_forge_install() call + define static ForgeConfig instances

**Files:**
- Modify: `src/version/install.c:17-17` (add static ForgeConfig instances after includes)
- Modify: `src/version/install.c:182-187` (replace "Coming soon!" block)

**Interfaces:**
- Consumes: `ForgeConfig` from install.h
- Produces: Wired install_page → do_forge_install call (function still stub, won't link)

- [ ] **Step 1: Add static ForgeConfig instances for Forge and NeoForge**

Add after the `#define MAX_RETRIES 2` line (line 17):

```c
static const ForgeConfig FORGE_CFG = {
  .maven_base = "maven.minecraftforge.net",
  .group_id   = "net.minecraftforge",
  .artifact_id = "forge",
  .has_promotions = 1,
};

static const ForgeConfig NEOFORGE_CFG = {
  .maven_base = "maven.neoforged.net",
  .group_id   = "net.neoforged",
  .artifact_id = "neoforge",
  .has_promotions = 0,
};
```

- [ ] **Step 2: Replace "Coming soon!" block with do_forge_install call**

Replace lines 182-187 in `install_page()` (the `if (modloader_sel >= 2)` block):

```c
          if (modloader_sel >= 2) { // forge / neoforge
            const ForgeConfig *fcfg =
                (modloader_sel == 2) ? &FORGE_CFG : &NEOFORGE_CFG;
            do_forge_install(cstate, selected_version, selected_url,
                             user_name, modloaders[modloader_sel],
                             types[s.type_sel], fcfg);
            goto quit;
          } else {
```

Keep lines 188-192 (the existing else/do_install block) unchanged.

- [ ] **Step 3: Add stub do_forge_install() so it compiles**

Add at the end of install.c (before the final blank line, or anywhere after `do_install()`):

```c
void do_forge_install(ConfigState *cstate, const char *mc_version,
                      const char *vanilla_url, const char *custom_name,
                      const char *modloader, const char *type,
                      const ForgeConfig *fcfg) {
  (void)cstate; (void)mc_version; (void)vanilla_url;
  (void)custom_name; (void)type;
  endwin();
  printf("\n=== Installing %s (%s) via %s ===\n", custom_name, mc_version,
         fcfg->artifact_id);
  printf("Maven: https://%s/%s/%s/\n", fcfg->maven_base, fcfg->group_id,
         fcfg->artifact_id);
  printf("Coming soon — stub only.\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {}
}
```

- [ ] **Step 4: Build and verify**

```bash
make clean && make
```
Expected: Compiles successfully. Running `./tmcl` → Install → pick MC version → pick forge/neoforge → Install → shows stub message.

---

### Task 3: Implement parse_maven_xml() — Maven metadata XML parser

**Files:**
- Modify: `src/version/install.c` — add function before `do_forge_install()` stub

**Interfaces:**
- Consumes: (none from earlier tasks — standalone helper)
- Produces: `static char **parse_maven_xml(const char *xml_data, const char *mc_version, int *count_out)` — returns malloc'd array of strdup'd version strings, caller frees each + array

- [ ] **Step 1: Write the function**

Add after `pick_fabric_loader()` (line ~718), before `do_install()`:

```c
// ======================== Maven metadata XML 解析 ========================
// 从 maven-metadata.xml 中提取所有匹配 mc_version 前缀的版本号
// 返回 strdup 的字符串数组，*count_out 为数量，调用者负责释放
static char **parse_maven_xml(const char *xml_data, const char *mc_version,
                              int *count_out) {
  *count_out = 0;
  if (!xml_data || !mc_version) return NULL;

  // 两遍扫描：先数匹配的，再分配
  int cap = 0;
  char **result = NULL;
  const char *p = xml_data;
  char prefix[64];
  snprintf(prefix, sizeof(prefix), "%s-", mc_version);
  size_t plen = strlen(prefix);

  while (*p) {
    // 找 <version> 标签
    const char *tag = strstr(p, "<version>");
    if (!tag) break;
    tag += 9; // 跳过 "<version>"
    const char *end = strstr(tag, "</version>");
    if (!end) break;

    size_t vlen = end - tag;
    if (vlen > 0 && vlen < 128 &&
        strncmp(tag, prefix, plen) == 0) {
      // 匹配：扩展容量
      if (*count_out >= cap) {
        cap = cap ? cap * 2 : 32;
        result = realloc(result, cap * sizeof(char *));
      }
      char *ver = malloc(vlen + 1);
      memcpy(ver, tag, vlen);
      ver[vlen] = '\0';
      result[*count_out] = ver;
      (*count_out)++;
    }
    p = end + 10; // 跳过 "</version>"
  }

  // 反转数组使新版本在前（XML 中旧版本在前）
  if (result && *count_out > 1) {
    for (int i = 0; i < *count_out / 2; i++) {
      char *tmp = result[i];
      result[i] = result[*count_out - 1 - i];
      result[*count_out - 1 - i] = tmp;
    }
  }
  return result;
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles. Stub do_forge_install still shows stub message (parser not wired yet).

---

### Task 4: Implement pick_forge_version() — Forge/NeoForge version selector UI

**Files:**
- Modify: `src/version/install.c` — add function after `parse_maven_xml()`

**Interfaces:**
- Consumes: (none)
- Produces: `static char *pick_forge_version(char **versions, int count, const ForgeConfig *fcfg)` — returns strdup'd selected version, or NULL on cancel

- [ ] **Step 1: Write the version selector UI**

Add after `parse_maven_xml()`:

```c
// ======================== Forge/NeoForge 版本选择器 ========================
static char *pick_forge_version(char **versions, int count,
                                const ForgeConfig *fcfg) {
  int sel = 0, scroll = 0;
  int row, col;
  char label[32];
  snprintf(label, sizeof(label), "%s", fcfg->artifact_id);
  // 首字母大写
  if (label[0] >= 'a' && label[0] <= 'z') label[0] -= 32;

  while (1) {
    clear();
    getmaxyx(stdscr, row, col);
    mvprintw(2, 4, "Select %s version (%d available):", label, count);
    int max_disp = row - 6;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + max_disp) scroll = sel - max_disp + 1;
    if (scroll < 0) scroll = 0;

    for (int i = scroll; i < count && i < scroll + max_disp; i++) {
      if (i == sel) attron(A_REVERSE);
      mvprintw(4 + i - scroll, 6, "%s", versions[i]);
      if (i == sel) attroff(A_REVERSE);
    }
    if (scroll > 0) mvprintw(3, 6, "... more above ...");
    if (scroll + max_disp < count)
      mvprintw(4 + max_disp, 6, "... more below ...");
    mvprintw(row - 1, 4, "j/k: move  Enter: confirm  q: cancel");

    int c = getch();
    if (c == 'j' && sel < count - 1) sel++;
    if (c == 'k' && sel > 0) sel--;
    if (c == '\n') break;
    if (c == 'q') { clear(); return NULL; }
  }
  clear();
  return strdup(versions[sel]);
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles (unused static function warning OK; not wired yet).

---

### Task 5: Implement get_jar_main_class() — extract Main-Class from MANIFEST.MF

**Files:**
- Modify: `src/version/install.c` — add function after `pick_forge_version()`

**Interfaces:**
- Consumes: (none)
- Produces: `static int get_jar_main_class(const char *jar_path, char *out, size_t out_size)` — returns 1 on success, 0 on failure

- [ ] **Step 1: Write the Main-Class extraction function**

```c
// ======================== 从 JAR 的 MANIFEST.MF 提取 Main-Class ========================
static int get_jar_main_class(const char *jar_path, char *out, size_t out_size) {
  out[0] = '\0';
  if (!jar_path || access(jar_path, R_OK) != 0) return 0;

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "unzip -p '%s' META-INF/MANIFEST.MF 2>/dev/null",
           jar_path);
  FILE *fp = popen(cmd, "r");
  if (!fp) return 0;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    // 跳过续行（以空格开头）
    if (line[0] == ' ') continue;
    // 匹配 "Main-Class: <classname>"
    if (strncmp(line, "Main-Class:", 11) == 0) {
      char *val = line + 11;
      // 跳过前导空格
      while (*val == ' ' || *val == '\t') val++;
      // 去除尾部 \r \n
      size_t vlen = strlen(val);
      while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r'))
        val[--vlen] = '\0';
      if (vlen > 0 && vlen < out_size) {
        memcpy(out, val, vlen);
        out[vlen] = '\0';
        pclose(fp);
        return 1;
      }
    }
  }
  pclose(fp);
  return 0;
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles.

---

### Task 6: Implement resolve_data_value() — resolve install_profile data placeholders

**Files:**
- Modify: `src/version/install.c` — add function after `get_jar_main_class()`

**Interfaces:**
- Consumes: (none from earlier tasks)
- Produces: `static char *resolve_data_value(const char *raw, const char *mcdir, const char *version_dir, const char *custom_name, cJSON *data, cJSON *outputs)` — returns malloc'd resolved path/value, caller frees; `custom_name` used for vanilla jar path resolution

- [ ] **Step 1: Write resolve_data_value()**

```c
// ======================== 解析 install_profile data 段中的值 ========================
// raw: 原始值，可能是 "{MINECRAFT_JAR}" 或 "[group:artifact:version:classifier@ext]" 或纯字符串
// data: install_profile 的 data JSON 对象
// outputs: 已执行 processor 的 outputs 累积映射
// 返回 malloc 的解析后路径/值，调用者负责 free
static char *resolve_data_value(const char *raw, const char *mcdir,
                                const char *version_dir, const char *custom_name,
                                cJSON *data, cJSON *outputs) {
  if (!raw) return strdup("");

  // 1) 检查是否为 {KEY} 占位符
  if (raw[0] == '{') {
    const char *end = strchr(raw, '}');
    if (end) {
      size_t klen = end - raw - 1;
      char key[64] = {0};
      if (klen < sizeof(key)) memcpy(key, raw + 1, klen);

      // ROOT 占位符 → 直接返回版本目录（data 段中 ROOT 是自引用的 {ROOT}）
      if (strcmp(key, "ROOT") == 0) {
        return strdup(version_dir);
      }

      // 先检查 outputs（processor 输出映射覆盖 data）
      if (outputs) {
        cJSON *ov = cJSON_GetObjectItem(outputs, raw);
        if (ov && cJSON_IsString(ov)) {
          return strdup(ov->valuestring);
        }
      }

      // 再查 data 段
      if (data) {
        cJSON *dv = cJSON_GetObjectItem(data, key);
        if (dv) {
          cJSON *client_val = cJSON_GetObjectItem(dv, "client");
          if (client_val && cJSON_IsString(client_val)) {
            // 防止循环：如果 data 值等于自身 key（如 "ROOT": "{ROOT}"），已在上方处理
            if (strcmp(client_val->valuestring, raw) == 0) {
              return strdup(version_dir); // 回退到版本目录
            }
            // 递归解析（值可能是 [maven:coord] 格式）
            return resolve_data_value(client_val->valuestring,
                                      mcdir, version_dir, custom_name,
                                      data, outputs);
          }
        }
      }
      // 未找到：返回占位符原文
      return strdup(raw);
    }
  }

  // 2) 检查是否为 [group:artifact:version:classifier@ext] 格式
  if (raw[0] == '[' && raw[strlen(raw) - 1] == ']') {
    size_t rlen = strlen(raw);
    char *inner = malloc(rlen); // rlen-2+1
    memcpy(inner, raw + 1, rlen - 2);
    inner[rlen - 2] = '\0';

    char *sp;
    char *group = strtok_r(inner, ":", &sp);
    char *artifact = strtok_r(NULL, ":", &sp);
    char *version = strtok_r(NULL, ":", &sp);
    char *classifier = strtok_r(NULL, ":", &sp); // 可能为 NULL

    char *result = NULL;
    if (group && artifact && version) {
      char gp[256];
      snprintf(gp, sizeof(gp), "%s", group);
      for (char *gp2 = gp; *gp2; gp2++)
        if (*gp2 == '.') *gp2 = '/';

      // 特殊处理：net.minecraft:minecraft:*:client → 原版 client.jar
      if (strcmp(group, "net.minecraft") == 0 &&
          strcmp(artifact, "minecraft") == 0) {
        asprintf(&result, "%s/%s.jar", version_dir, custom_name);
      } else {
        // 普通 Maven 库
        // 处理 @ext 后缀（可能在 version 或 classifier 中）
        char *at_sign = strchr(version, '@');
        char ext[16] = "jar";
        if (at_sign) {
          snprintf(ext, sizeof(ext), "%s", at_sign + 1);
          *at_sign = '\0';
        }
        if (classifier) {
          at_sign = strchr(classifier, '@');
          if (at_sign) {
            snprintf(ext, sizeof(ext), "%s", at_sign + 1);
            *at_sign = '\0';
          }
        }

        if (classifier) {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s.%s",
                   mcdir, gp, artifact, version, artifact,
                   classifier, ext);
        } else {
          asprintf(&result, "%s/libraries/%s/%s/%s/%s-%s.%s",
                   mcdir, gp, artifact, version, artifact, version, ext);
        }
      }
    }
    free(inner);
    return result ? result : strdup(raw);
  }

  // 3) 纯字符串，直接返回
  return strdup(raw);
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles.

---

### Task 7: Implement run_forge_processor() — execute a single processor

**Files:**
- Modify: `src/version/install.c` — add function after `resolve_data_value()`

**Interfaces:**
- Consumes: `get_jar_main_class()`, `resolve_data_value()`
- Produces: `static int run_forge_processor(cJSON *proc, const char *mcdir, const char *version_dir, const char *custom_name, cJSON *data, cJSON **outputs)` — returns 0 on success, 1 on failure

- [ ] **Step 1: Write run_forge_processor()**

```c
// ======================== 执行单个 Forge processor ========================
static int run_forge_processor(cJSON *proc, const char *mcdir,
                               const char *version_dir, const char *custom_name,
                               cJSON *data, cJSON **outputs) {
  if (g_cancel) return 1;

  // 1) 检查 sides：只处理 "client"
  cJSON *sides = cJSON_GetObjectItem(proc, "sides");
  if (sides && cJSON_IsArray(sides)) {
    int has_client = 0;
    cJSON *side;
    cJSON_ArrayForEach(side, sides) {
      if (cJSON_IsString(side) && strcmp(side->valuestring, "client") == 0) {
        has_client = 1;
        break;
      }
    }
    if (!has_client) return 0; // 跳过 server-only processor
  }

  // 2) 获取 processor jar 路径
  cJSON *jar_name = cJSON_GetObjectItem(proc, "jar");
  if (!jar_name || !cJSON_IsString(jar_name)) {
    printf("  ERROR: processor missing 'jar' field\n");
    return 1;
  }
  char *jar_path = resolve_data_value(jar_name->valuestring,
                                      mcdir, version_dir, custom_name,
                                      data, *outputs);
  if (!jar_path || access(jar_path, R_OK) != 0) {
    printf("  ERROR: processor jar not found: %s\n",
           jar_path ? jar_path : "(null)");
    free(jar_path);
    return 1;
  }

  // 3) 提取 Main-Class
  char main_class[256] = {0};
  if (!get_jar_main_class(jar_path, main_class, sizeof(main_class))) {
    printf("  ERROR: failed to extract Main-Class from %s\n", jar_path);
    free(jar_path);
    return 1;
  }
  printf("  Processor: %s\n", main_class);

  // 4) 构建 classpath：processor jar + classpath 数组
  char classpath[32768] = {0};
  snprintf(classpath, sizeof(classpath), "%s", jar_path);

  cJSON *cp_arr = cJSON_GetObjectItem(proc, "classpath");
  if (cp_arr && cJSON_IsArray(cp_arr)) {
    cJSON *cp_item;
    cJSON_ArrayForEach(cp_item, cp_arr) {
      if (!cJSON_IsString(cp_item)) continue;
      char *cp_path = resolve_data_value(cp_item->valuestring,
                                         mcdir, version_dir, custom_name,
                                         data, *outputs);
      if (cp_path) {
        size_t cur = strlen(classpath);
        snprintf(classpath + cur, sizeof(classpath) - cur, ":%s", cp_path);
        free(cp_path);
      }
    }
  }

  // 5) 构建 args 数组
  char *args[256];
  int ac = 0;
  args[ac++] = "java";
  args[ac++] = "-cp";
  args[ac++] = classpath;
  args[ac++] = main_class;

  cJSON *pargs = cJSON_GetObjectItem(proc, "args");
  if (pargs && cJSON_IsArray(pargs)) {
    cJSON *arg_item;
    cJSON_ArrayForEach(arg_item, pargs) {
      if (!cJSON_IsString(arg_item) || ac >= 250) continue;
      char *resolved = resolve_data_value(arg_item->valuestring,
                                          mcdir, version_dir, custom_name,
                                          data, *outputs);
      if (resolved) {
        args[ac++] = resolved;
      }
    }
  }
  args[ac] = NULL;

  // 6) 打印命令（调试用）
  printf("  CMD:");
  for (int i = 0; i < ac; i++) printf(" %s", args[i]);
  printf("\n");

  // 7) fork/exec
  pid_t pid = fork();
  if (pid < 0) {
    printf("  ERROR: fork failed\n");
    free(jar_path);
    return 1;
  }
  if (pid == 0) {
    execvp("java", args);
    perror("execvp java");
    _exit(1);
  }

  // 8) 等待完成，支持 SIGINT 取消
  int status, done = 0;
  while (!done) {
    if (g_cancel) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      printf("  CANCELLED\n");
      free(jar_path);
      return 1;
    }
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r < 0) { if (errno == EINTR) continue; break; }
    if (r > 0) { done = 1; break; }
    usleep(100000);
  }

  if (!done) { printf("  ERROR: waitpid failed\n"); free(jar_path); return 1; }

  // 9) 释放 args 中动态分配的字符串（索引 3 及以后是 resolve_data_value 返回的）
  for (int i = 3; i < ac; i++) free(args[i]);
  free(jar_path);

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("  OK\n");
    // 10) 更新 outputs 映射
    cJSON *proc_outputs = cJSON_GetObjectItem(proc, "outputs");
    if (proc_outputs && cJSON_IsObject(proc_outputs)) {
      if (!*outputs) *outputs = cJSON_CreateObject();
      cJSON *child;
      cJSON_ArrayForEach(child, proc_outputs) {
        // 将 outputs 的 key/value 复制到累积 outputs
        // key: "{SIDE_STRIPPER_OUTPUT}" value: "binpatches.pack"（相对于 ROOT）
        if (cJSON_IsString(child)) {
          const char *okey = child->string;
          const char *oval = child->valuestring;
          // 如果值不含路径分隔符，视为相对于 version_dir
          if (!strchr(oval, '/')) {
            char full_path[600];
            snprintf(full_path, sizeof(full_path), "%s/%s", version_dir, oval);
            cJSON_AddStringToObject(*outputs, okey, full_path);
          } else {
            cJSON_AddStringToObject(*outputs, okey, oval);
          }
        }
      }
    }
    return 0;
  } else {
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    printf("  FAILED (exit code: %d)\n", code);
    return 1;
  }
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles. (asprintf may need `_GNU_SOURCE`; if it fails, replace with malloc+snprintf)

---

### Task 8: Implement merge_forge_version_json() — merge vanilla + Forge into self-contained JSON

**Files:**
- Modify: `src/version/install.c` — add function after `run_forge_processor()`

**Interfaces:**
- Consumes: cJSON
- Produces: `static int merge_forge_version_json(cJSON *vanilla, cJSON *version_info, const char *custom_name, const char *mc_version, const char *modloader, const char *forge_version, const char *json_path)` — returns 0 on success, 1 on failure

- [ ] **Step 1: Write merge_forge_version_json()**

```c
// ======================== 合并 vanilla + Forge version JSON ========================
static int merge_forge_version_json(cJSON *vanilla, cJSON *version_info,
                                     const char *custom_name,
                                     const char *mc_version,
                                     const char *modloader,
                                     const char *forge_version,
                                     const char *json_path) {
  if (!vanilla || !version_info) return 1;

  // a) 覆盖 mainClass（从 versionInfo 取）
  cJSON *vi_main = cJSON_GetObjectItem(version_info, "mainClass");
  if (vi_main && cJSON_IsString(vi_main)) {
    cJSON_DeleteItemFromObject(vanilla, "mainClass");
    cJSON_AddStringToObject(vanilla, "mainClass", vi_main->valuestring);
  }

  // b) 合并 arguments
  cJSON *vi_args = cJSON_GetObjectItem(version_info, "arguments");
  cJSON *v_args = cJSON_GetObjectItem(vanilla, "arguments");
  if (vi_args && v_args) {
    // JVM args — 追加 Forge JVM 参数到原版
    cJSON *vi_jvm = cJSON_GetObjectItem(vi_args, "jvm");
    cJSON *v_jvm = cJSON_GetObjectItem(v_args, "jvm");
    if (vi_jvm && cJSON_IsArray(vi_jvm) && v_jvm && cJSON_IsArray(v_jvm)) {
      cJSON *jv;
      cJSON_ArrayForEach(jv, vi_jvm) {
        cJSON_AddItemToArray(v_jvm, cJSON_Duplicate(jv, 1));
      }
    }
    // Game args — 追加 Forge game 参数
    cJSON *vi_game = cJSON_GetObjectItem(vi_args, "game");
    cJSON *v_game = cJSON_GetObjectItem(v_args, "game");
    if (vi_game && cJSON_IsArray(vi_game) && v_game && cJSON_IsArray(v_game)) {
      cJSON *gv;
      cJSON_ArrayForEach(gv, vi_game) {
        cJSON_AddItemToArray(v_game, cJSON_Duplicate(gv, 1));
      }
    }
  }

  // c) 合并 libraries（去重：同 groupId:artifactId 保留 Forge 版本）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs) && v_libs && cJSON_IsArray(v_libs)) {
    // 收集 versionInfo libraries 的 groupId:artifactId
    int vi_count = cJSON_GetArraySize(vi_libs);
    char **vi_ga = malloc(vi_count * sizeof(char *));
    for (int i = 0; i < vi_count; i++) {
      vi_ga[i] = NULL;
      cJSON *lib = cJSON_GetArrayItem(vi_libs, i);
      cJSON *nm = cJSON_GetObjectItem(lib, "name");
      if (nm && cJSON_IsString(nm)) {
        char *ns = strdup(nm->valuestring);
        char *first_colon = strchr(ns, ':');
        if (first_colon) {
          char *second_colon = strchr(first_colon + 1, ':');
          if (second_colon) *second_colon = '\0';
        }
        vi_ga[i] = ns;
      }
    }
    // 移除原版中重复的 library（从后往前遍历）
    int v_count = cJSON_GetArraySize(v_libs);
    for (int i = v_count - 1; i >= 0; i--) {
      cJSON *lib = cJSON_GetArrayItem(v_libs, i);
      cJSON *nm = cJSON_GetObjectItem(lib, "name");
      if (nm && cJSON_IsString(nm)) {
        char *ns = strdup(nm->valuestring);
        char *fc = strchr(ns, ':');
        if (fc) {
          char *sc = strchr(fc + 1, ':');
          if (sc) *sc = '\0';
        }
        for (int j = 0; j < vi_count; j++) {
          if (vi_ga[j] && strcmp(ns, vi_ga[j]) == 0) {
            cJSON_DeleteItemFromArray(v_libs, i);
            break;
          }
        }
        free(ns);
      }
    }
    for (int i = 0; i < vi_count; i++) free(vi_ga[i]);
    free(vi_ga);
    // 追加 versionInfo libraries
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      cJSON_AddItemToArray(v_libs, cJSON_Duplicate(lib, 1));
    }
  }

  // d) 移除 inheritsFrom
  cJSON_DeleteItemFromObject(vanilla, "inheritsFrom");

  // e) 设置 id
  cJSON_DeleteItemFromObject(vanilla, "id");
  cJSON_AddStringToObject(vanilla, "id", custom_name);

  // f) 添加元数据（init_versions 用 forge/neoforge + gameVersion 字段检测）
  cJSON_AddStringToObject(vanilla, "gameVersion", mc_version);
  if (strcmp(modloader, "neoforge") == 0) {
    cJSON *neo_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(neo_meta, "version", forge_version);
    cJSON_AddItemToObject(vanilla, "neoforge", neo_meta);
  } else {
    cJSON *forge_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(forge_meta, "version", forge_version);
    cJSON_AddItemToObject(vanilla, "forge", forge_meta);
  }

  // g) 写回文件
  char *merged = cJSON_Print(vanilla);
  if (!merged) return 1;
  FILE *mf = fopen(json_path, "w");
  if (!mf) { free(merged); return 1; }
  fputs(merged, mf);
  fclose(mf);
  free(merged);
  printf("  OK  merged %s + vanilla JSON\n", modloader);
  return 0;
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make
```
Expected: Compiles.

---

### Task 9: Implement do_forge_install() — main Forge/NeoForge installation orchestrator

**Files:**
- Modify: `src/version/install.c` — replace the stub `do_forge_install()` with full implementation

**Interfaces:**
- Consumes: All previous task functions plus existing `dl_one()`, `dl_parallel()`, `retry_failed()`, `build_lib_task()`, `lib_rules_ok()`, `on_sigint()`/`g_cancel`
- Produces: Working Forge/NeoForge installation

- [ ] **Step 1: Replace stub do_forge_install() with full implementation**

Replace the entire stub function:

```c
// ======================== Forge/NeoForge 安装主函数 ========================
void do_forge_install(ConfigState *cstate, const char *mc_version,
                      const char *vanilla_url, const char *custom_name,
                      const char *modloader, const char *type,
                      const ForgeConfig *fcfg) {
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1) num_threads = 96;
  char *mcdir = cstate->items[2].value;

  printf("\n=== Installing %s (%s / %s) ===\n", custom_name, mc_version,
         fcfg->artifact_id);
  printf("    Threads: 8 (libs) / %d (assets)\n\n", num_threads);

  // 创建版本目录
  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);

  // ---- Phase 0: 拉取 Maven metadata，选择 Forge/NeoForge 版本 ----
  char meta_url[1024];
  snprintf(meta_url, sizeof(meta_url),
           "https://%s/%s/%s/maven-metadata.xml",
           fcfg->maven_base, fcfg->group_id, fcfg->artifact_id);
  // 将 group_id 中的点替换为斜杠
  for (char *p = meta_url; *p; p++) {
    // 只在 "https://" 之后的部分做替换
    if (p > meta_url + 8 && *p == '.') *p = '/';
  }

  printf("  Fetching %s versions...\n", fcfg->artifact_id);
  char cmd[1536];
  snprintf(cmd, sizeof(cmd), "curl -sL -o /tmp/tmcl_forge_meta.xml '%s'",
           meta_url);
  if (system(cmd) != 0) {
    printf("ERROR: Failed to fetch Maven metadata.\n");
    goto done;
  }

  FILE *mf = fopen("/tmp/tmcl_forge_meta.xml", "r");
  if (!mf) { printf("ERROR: Failed to read Maven metadata.\n"); goto done; }
  fseek(mf, 0, SEEK_END);
  long msz = ftell(mf);
  fseek(mf, 0, SEEK_SET);
  char *mdata = malloc(msz + 1);
  if (!mdata) { fclose(mf); goto done; }
  fread(mdata, 1, msz, mf);
  mdata[msz] = '\0';
  fclose(mf);

  int fver_count = 0;
  char **fversions = parse_maven_xml(mdata, mc_version, &fver_count);
  free(mdata);
  if (!fversions || fver_count == 0) {
    printf("ERROR: No %s versions found for MC %s\n", fcfg->artifact_id,
           mc_version);
    goto done;
  }

  char *chosen_forge_ver = pick_forge_version(fversions, fver_count, fcfg);
  for (int i = 0; i < fver_count; i++) free(fversions[i]);
  free(fversions);
  if (!chosen_forge_ver) {
    printf("  Cancelled by user.\n");
    goto done;
  }
  printf("  Selected: %s %s\n", fcfg->artifact_id, chosen_forge_ver);

  // ---- Phase 1: 下载 installer jar 并提取 install_profile.json ----
  char installer_url[1024];
  // 重新构建 meta_url 的路径部分（不包含 maven-metadata.xml）
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar",
           fcfg->maven_base, fcfg->group_id, fcfg->artifact_id,
           chosen_forge_ver, fcfg->artifact_id, chosen_forge_ver);
  // 替换 group_id 中的点
  char *proto_end = strstr(installer_url, "://");
  if (proto_end) {
    for (char *p = proto_end + 3; *p; p++)
      if (*p == '.') *p = '/';
  }

  char installer_path[600];
  snprintf(installer_path, sizeof(installer_path),
           "/tmp/tmcl_forge_installer_%s.jar", chosen_forge_ver);
  printf("  Downloading installer...\n");
  if (dl_one(installer_url, installer_path, "installer jar") != 0) {
    printf("ERROR: Failed to download installer jar.\n");
    free(chosen_forge_ver);
    goto done;
  }

  // 提取 install_profile.json
  printf("  Extracting install_profile.json...\n");
  char extract_cmd[1536];
  snprintf(extract_cmd, sizeof(extract_cmd),
           "unzip -p '%s' install_profile.json 2>/dev/null",
           installer_path);
  FILE *ep = popen(extract_cmd, "r");
  char *profile_raw = NULL;
  size_t pcap = 0, plen = 0;
  if (ep) {
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), ep)) {
      size_t cl = strlen(chunk);
      if (plen + cl + 1 > pcap) {
        pcap = pcap ? pcap * 2 : 16384;
        profile_raw = realloc(profile_raw, pcap);
      }
      memcpy(profile_raw + plen, chunk, cl);
      plen += cl;
      profile_raw[plen] = '\0';
    }
    pclose(ep);
  }
  remove(installer_path); // 清理 installer jar（不再需要）

  if (!profile_raw) {
    printf("ERROR: Failed to extract install_profile.json.\n");
    free(chosen_forge_ver);
    goto done;
  }

  cJSON *install_profile = cJSON_Parse(profile_raw);
  free(profile_raw);
  if (!install_profile) {
    printf("ERROR: Failed to parse install_profile.json.\n");
    free(chosen_forge_ver);
    goto done;
  }

  cJSON *version_info = cJSON_GetObjectItem(install_profile, "versionInfo");
  cJSON *profile_libs = cJSON_GetObjectItem(install_profile, "libraries");
  cJSON *processors = cJSON_GetObjectItem(install_profile, "processors");
  cJSON *data = cJSON_GetObjectItem(install_profile, "data");

  if (!version_info) {
    printf("ERROR: install_profile.json missing 'versionInfo'.\n");
    cJSON_Delete(install_profile);
    free(chosen_forge_ver);
    goto done;
  }

  // ---- Phase 2: 下载原版 vanilla JSON ----
  printf("  Downloading vanilla JSON...\n");
  char vanilla_json_path[600];
  snprintf(vanilla_json_path, sizeof(vanilla_json_path),
           "/tmp/tmcl_vanilla_%s.json", mc_version);
  if (dl_one(vanilla_url, vanilla_json_path, "vanilla JSON") != 0) {
    printf("ERROR: Failed to download vanilla JSON.\n");
    cJSON_Delete(install_profile);
    free(chosen_forge_ver);
    goto done;
  }

  // 读取 vanilla JSON 到内存
  FILE *vf = fopen(vanilla_json_path, "r");
  cJSON *vanilla = NULL;
  if (vf) {
    fseek(vf, 0, SEEK_END);
    long vsz = ftell(vf);
    fseek(vf, 0, SEEK_SET);
    char *vd = malloc(vsz + 1);
    if (vd) { fread(vd, 1, vsz, vf); vd[vsz] = '\0'; vanilla = cJSON_Parse(vd); free(vd); }
    fclose(vf);
  }
  if (!vanilla) {
    printf("ERROR: Failed to parse vanilla JSON.\n");
    cJSON_Delete(install_profile);
    free(chosen_forge_ver);
    goto done;
  }

  // ---- Phase 3: 收集所有下载任务 ----
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  int tcount = 0;

  // a) 原版 libraries（从 vanilla JSON）
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  int vlib_count = 0;
  if (v_libs && cJSON_IsArray(v_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, v_libs) {
      if (lib_rules_ok(cJSON_GetObjectItem(lib, "rules"))) vlib_count++;
    }
    int cur = 0;
    cJSON_ArrayForEach(lib, v_libs) {
      if (!lib_rules_ok(cJSON_GetObjectItem(lib, "rules"))) continue;
      cur++;
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[%d/%d] %s", cur, vlib_count, t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // b) 原版 client.jar
  cJSON *downloads = cJSON_GetObjectItem(vanilla, "downloads");
  if (downloads) {
    cJSON *client = cJSON_GetObjectItem(downloads, "client");
    if (client) {
      cJSON *jar_url = cJSON_GetObjectItem(client, "url");
      if (jar_url && cJSON_IsString(jar_url)) {
        snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                 jar_url->valuestring);
        snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s/%s.jar",
                 version_dir, custom_name);
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                 "vanilla client.jar");
        tcount++;
      }
    }
  }

  // c) install_profile 顶层 libraries（processor 依赖）
  if (profile_libs && cJSON_IsArray(profile_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, profile_libs) {
      // 强制设置 Maven URL 为 Forge/NeoForge 的仓库
      cJSON *existing_url = cJSON_GetObjectItem(lib, "url");
      if (!existing_url || !cJSON_IsString(existing_url) ||
          !existing_url->valuestring[0]) {
        char lib_url[512];
        snprintf(lib_url, sizeof(lib_url), "https://%s/", fcfg->maven_base);
        cJSON_AddStringToObject(lib, "url", lib_url);
      }
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[forge] %s", t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // d) versionInfo libraries（运行时）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      cJSON *existing_url = cJSON_GetObjectItem(lib, "url");
      if (!existing_url || !cJSON_IsString(existing_url) ||
          !existing_url->valuestring[0]) {
        char lib_url[512];
        snprintf(lib_url, sizeof(lib_url), "https://%s/", fcfg->maven_base);
        cJSON_AddStringToObject(lib, "url", lib_url);
      }
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[forge] %s", t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }

  // e) asset index
  char ai_url[1024] = {0};
  char ai_path[512] = {0};
  cJSON *assetIndex = cJSON_GetObjectItem(vanilla, "assetIndex");
  if (assetIndex) {
    cJSON *ai_u = cJSON_GetObjectItem(assetIndex, "url");
    cJSON *ai_id = cJSON_GetObjectItem(assetIndex, "id");
    if (ai_u && cJSON_IsString(ai_u) && ai_id && cJSON_IsString(ai_id)) {
      snprintf(ai_url, sizeof(ai_url), "%s", ai_u->valuestring);
      snprintf(ai_path, sizeof(ai_path), "%s/assets/indexes/%s.json", mcdir,
               ai_id->valuestring);
      snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s", ai_url);
      snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s", ai_path);
      snprintf(tasks[tcount].label, sizeof(tasks[tcount].label), "asset index");
      tcount++;
    }
  }

  // ---- Phase 4: 并行下载所有文件 ----
  printf("  Downloading %d files...\n\n", tcount);
  dl_parallel(tasks, tcount, 8);

  // 重试失败的文件
  char **failed_names = NULL;
  int failed_count = 0;
  if (g_cancel) {
    printf("\n  Cancelled by user, skipping retries.\n");
    goto forge_cancelled;
  }
  for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
    int f = retry_failed(tasks, tcount, 8, &failed_names, &failed_count);
    if (f == 0) break;
  }

  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
    failed_names = NULL;
    failed_count = 0;
  }

  // ---- Phase 5: 顺序执行 processors ----
  if (!g_cancel && processors && cJSON_IsArray(processors)) {
    int ptotal = cJSON_GetArraySize(processors);
    printf("\n  Running %d processor(s)...\n\n", ptotal);
    cJSON *outputs = NULL;
    cJSON *proc;
    int pi = 0;
    cJSON_ArrayForEach(proc, processors) {
      if (g_cancel) break;
      pi++;
      printf("  [%d/%d]", pi, ptotal);
      if (run_forge_processor(proc, mcdir, version_dir, custom_name,
                              data, &outputs) != 0) {
        printf("  ERROR: Processor %d failed.\n", pi);
        if (outputs) cJSON_Delete(outputs);
        // 继续执行后续 processor（有些 processor 可选）
        outputs = NULL;
      }
    }
    if (outputs) cJSON_Delete(outputs);
  }

  // ---- Phase 6: 生成自包含 version JSON ----
  if (!g_cancel) {
    printf("\n  Merging version JSON...\n");
    if (merge_forge_version_json(vanilla, version_info, custom_name,
                                  mc_version, modloader, chosen_forge_ver,
                                  json_path) != 0) {
      printf("ERROR: Failed to merge version JSON.\n");
    }
  }

  // ---- Phase 7: 下载 assets ----
  if (ai_path[0] && !g_cancel) {
    FILE *aif = fopen(ai_path, "r");
    if (aif) {
      fseek(aif, 0, SEEK_END);
      long asz = ftell(aif);
      fseek(aif, 0, SEEK_SET);
      char *ai_data = malloc(asz + 1);
      if (ai_data) { fread(ai_data, 1, asz, aif); ai_data[asz] = '\0'; }
      fclose(aif);

      cJSON *ai_root = ai_data ? cJSON_Parse(ai_data) : NULL;
      if (ai_root) {
        cJSON *objects = cJSON_GetObjectItem(ai_root, "objects");
        if (objects && cJSON_IsObject(objects)) {
          int need_dl = 0, already = 0;
          cJSON *obj_item;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item)) continue;
            const char *hash = hash_item->valuestring;
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%c%c/%s", mcdir,
                     hash[0], hash[1], hash);
            if (access(dest, F_OK) == 0) already++;
            else need_dl++;
          }

          DlTask *atasks = malloc((need_dl + 1) * sizeof(DlTask));
          int ac = 0;
          cJSON_ArrayForEach(obj_item, objects) {
            cJSON *hash_item = cJSON_GetObjectItem(obj_item, "hash");
            if (!hash_item || !cJSON_IsString(hash_item)) continue;
            const char *hash = hash_item->valuestring;
            char first2[3] = {hash[0], hash[1], '\0'};
            char dest[600];
            snprintf(dest, sizeof(dest), "%s/assets/objects/%s/%s", mcdir,
                     first2, hash);
            if (access(dest, F_OK) == 0) continue;
            snprintf(atasks[ac].url, sizeof(atasks[ac].url),
                     "https://resources.download.minecraft.net/%s/%s", first2,
                     hash);
            snprintf(atasks[ac].dest, sizeof(atasks[ac].dest), "%s", dest);
            snprintf(atasks[ac].label, sizeof(atasks[ac].label),
                     "[%d/%d] %s", ac + 1, need_dl, obj_item->string);
            ac++;
          }
          if (ac > 0) {
            printf("\n  Assets: %d need download (%d already cached)\n\n",
                   ac, already);
            dl_parallel(atasks, ac, num_threads);
            for (int rtry = 0; rtry < MAX_RETRIES && !g_cancel; rtry++) {
              char **a_fail = NULL;
              int a_fc = 0;
              int f = retry_failed(atasks, ac, num_threads, &a_fail, &a_fc);
              if (f == 0) break;
              if (a_fc > 0) {
                failed_names = realloc(failed_names,
                    (failed_count + a_fc) * sizeof(char *));
                for (int i = 0; i < a_fc; i++)
                  failed_names[failed_count++] = a_fail[i];
                free(a_fail);
              }
            }
          } else {
            printf("\n  Assets: all %d already cached\n", already);
          }
          free(atasks);
        }
        cJSON_Delete(ai_root);
      }
      free(ai_data);
    }
  }

forge_cancelled:
  // 清理
  free(tasks);
  cJSON_Delete(vanilla);
  cJSON_Delete(install_profile);
  free(chosen_forge_ver);
  remove(vanilla_json_path);

  // 打印失败总结
  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
    printf("\nYou may run the installer again to retry missing files.\n");
  }

  printf("\n=== Installation complete: %s ===\n", custom_name);
  if (g_cancel) printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {}

done:
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}
```

- [ ] **Step 2: Ensure asprintf is available**

If the compiler complains about `asprintf`, add `#define _GNU_SOURCE` at the very top of install.c (before any `#include`):

```c
#define _GNU_SOURCE
```

Check that `#include <stdio.h>` is already there (it is, line 5).

- [ ] **Step 3: Build**

```bash
make clean && make
```

Expected: Compiles successfully. Fix any warnings/errors.

---

### Task 10: Fix `_GNU_SOURCE` and `asprintf` compilation, build, and functional test

**Files:**
- Possibly modify: `src/version/install.c:1` (add `#define _GNU_SOURCE`)

- [ ] **Step 1: Add _GNU_SOURCE if needed**

If the build in Task 9 Step 3 fails with `implicit declaration of function 'asprintf'`, add at line 1 of `src/version/install.c`:

```c
#define _GNU_SOURCE
```

- [ ] **Step 2: Rebuild and verify clean compilation**

```bash
make clean && make
```

Expected: Zero warnings, successful link.

- [ ] **Step 3: Functional test — install a Forge version**

```bash
./tmcl
```

Manual test flow:
1. Navigate to Version page (press `V`)
2. Press `n` for new version
3. Select type `release`
4. Select MC version `1.20.1`
5. Select mod loader `forge`
6. Observe version selector appears with available Forge 1.20.1 versions
7. Select a version (e.g., `1.20.1-47.2.0`)
8. Enter a name or use auto-generated
9. Press Enter on `[ start installation ]`
10. Observe: Maven metadata fetched → installer downloaded → libs downloaded → processors execute → JSON merged → assets downloaded
11. Verify: version appears in version list with modloader `forge`
12. Try to launch the version

- [ ] **Step 4: Functional test — install a NeoForge version**

Same flow, but select `neoforge` as mod loader.

- [ ] **Step 5: Verify JSON output**

```bash
cat ~/.minecraft/versions/<custom_name>/<custom_name>.json | head -30
```

Expected: No `inheritsFrom` field, has `mainClass: "cpw.mods.modlauncher.Launcher"`, has `forge` or `neoforge` metadata section.

- [ ] **Step 6: Commit**

```bash
git add src/version/install.c src/version/install.h
git commit -m "feat: implement Forge/NeoForge installation

- Add ForgeConfig struct for unified Forge/NeoForge handling
- Implement Maven metadata XML parser (parse_maven_xml)
- Implement Forge version selector UI (pick_forge_version)
- Implement Main-Class extraction from JAR manifests (get_jar_main_class)
- Implement install_profile data placeholder resolver (resolve_data_value)
- Implement single processor executor (run_forge_processor)
- Implement vanilla+Forge JSON merger (merge_forge_version_json)
- Implement main Forge/NeoForge install orchestrator (do_forge_install)
- Wire install_page to call do_forge_install for forge/neoforge modloader"
```

---

### Task 11: Edge case handling and robustness

**Files:**
- Modify: `src/version/install.c`

- [ ] **Step 1: Handle promotions_slim.json for Forge (optional, adds recommended/latest markers)**

For Forge only (`fcfg->has_promotions`), fetch `https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json` and parse to get `recommended`/`latest` for the MC version. Skip for now — the version list is already informative enough. Add a comment noting this as future work.

- [ ] **Step 2: Handle URL dot-replacement in do_forge_install more robustly**

In `do_forge_install()`, the URL building manually replaces `.` with `/` in the group_id. This logic is fragile. Replace the manual `for` loop that walks the entire URL string with a targeted replacement that only operates on the group_id portion:

Find this code in `do_forge_install()`:
```c
  char meta_url[1024];
  snprintf(meta_url, sizeof(meta_url),
           "https://%s/%s/%s/maven-metadata.xml",
           fcfg->maven_base, fcfg->group_id, fcfg->artifact_id);
  // 将 group_id 中的点替换为斜杠
  for (char *p = meta_url; *p; p++) {
    // 只在 "https://" 之后的部分做替换
    if (p > meta_url + 8 && *p == '.') *p = '/';
  }
```

Replace with:
```c
  // 构建 Maven 路径：将 group_id 中的 . 替换为 /
  char group_path[256];
  snprintf(group_path, sizeof(group_path), "%s", fcfg->group_id);
  for (char *p = group_path; *p; p++)
    if (*p == '.') *p = '/';

  char meta_url[1024];
  snprintf(meta_url, sizeof(meta_url),
           "https://%s/%s/%s/maven-metadata.xml",
           fcfg->maven_base, group_path, fcfg->artifact_id);
```

Do the same for `installer_url` construction — use `group_path` instead of raw `group_id` in the URL. Find:
```c
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar",
           fcfg->maven_base, fcfg->group_id, fcfg->artifact_id,
           chosen_forge_ver, fcfg->artifact_id, chosen_forge_ver);
  // 替换 group_id 中的点
  char *proto_end = strstr(installer_url, "://");
  if (proto_end) {
    for (char *p = proto_end + 3; *p; p++)
      if (*p == '.') *p = '/';
  }
```

Replace with:
```c
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar",
           fcfg->maven_base, group_path, fcfg->artifact_id,
           chosen_forge_ver, fcfg->artifact_id, chosen_forge_ver);
```

- [ ] **Step 3: Handle NeoForge version format differences**

NeoForge versions changed format over time. In early 1.20.1 releases, versions were like `1.20.1-47.1.0` (same format as Forge). In later releases they changed to a beta numbering. Verify the XML parser handles any format — it should, since it just does prefix matching on `<mc_version>-`.

- [ ] **Step 4: Add download source override support**

The config has `download_cource` (item 4) with values `Official`/`BMCLAPI`. For Forge/NeoForge, if the user selected BMCLAPI, try to use `https://bmclapi2.bangbang93.com/maven/` as an alternative Maven base. Add this check at the top of `do_forge_install()`:

```c
  // 下载源覆盖（BMCLAPI 镜像）
  const char *dc = cstate->items[4].value;
  int use_bmcl = (strcmp(dc, "BMCLAPI") == 0);
```

Then when building URLs, use BMCLAPI prefix if enabled. For now, skip this (the Forge Maven is usually fast enough). Add a comment for future work.

- [ ] **Step 5: Build and verify**

```bash
make clean && make
```
Expected: Clean compilation.

- [ ] **Step 6: Commit**

```bash
git add src/version/install.c
git commit -m "fix: robust URL construction for Forge/NeoForge Maven paths"
```
