# 旧版 Forge 安装 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement old Forge (1.7.10–1.12.2, launchwrapper-based) installation as a separate code path `do_legacy_forge_install()`, reusing existing Maven/metadata/UI/download infrastructure.

**Architecture:** New standalone function `do_legacy_forge_install()` in `install.c` with its own declaration in `install.h`. `install_page()` detects legacy via a `static int is_legacy_forge(const char *mc_version)` helper that extracts the minor version number and compares it against 12. All phases reuse existing `build_lib_task()`, `dl_parallel()`, `retry_failed()`, `dl_one()`, `lib_rules_ok()`, `legacy_args()`. Zero changes to `version.c`.

**Tech Stack:** C (C99 + `_GNU_SOURCE` for `asprintf`), ncurses, cJSON, curl (via `dl_one`/`dl_parallel` popen), unzip (popen)

## Global Constraints

- Forge 1.7.10–1.12.2 only (launchwrapper system); not 1.13+ (use existing `do_forge_install`)
- Version JSON uses `inheritsFrom` (vanilla JSON auto-downloaded if missing)
- Maven URL: `maven.minecraftforge.net` (ForgeConfig.maven_base)
- `build_lib_task` already handles `group:artifact:version:classifier` format
- `legacy_args()` already handles `minecraftArguments` string format in `version.c`
- Forge modloader detection: add `"forge": {"version": "..."}` to version JSON (existing `init_versions` reads this)
- `g_cancel` flag checked during downloads and retries for SIGINT support
- All `malloc` must have corresponding `free`; use `snprintf` + `sizeof` for buffers
- HTTP forced to HTTPS in all download URLs (handled by `build_lib_task`)

## File Structure

| File | Role |
|---|---|
| `src/version/install.h` | Declaration of `do_legacy_forge_install()` |
| `src/version/install.c` | `is_legacy_forge()` helper, modified `install_page()` routing, `do_legacy_forge_install()` implementation |

No new files. No modifications to `version.c` or `version.h`.

---

### Task 1: Add `is_legacy_forge()` helper and `do_legacy_forge_install()` declaration

**Files:**
- Modify: `src/version/install.h:36-40` (before `#endif`)
- Modify: `src/version/install.c:55-58` (before `pick_forge_version` declaration)

**Interfaces:**
- Produces: `is_legacy_forge(const char *mc_version)` — returns 1 if legacy, 0 otherwise
- Produces: `do_legacy_forge_install()` — same signature as `do_forge_install()`

- [ ] **Step 1: Add function declaration to install.h**

In `src/version/install.h`, add after the `do_forge_install` declaration and before `#endif`:

```c
// 旧版 Forge 安装主函数（1.7.10–1.12.2，launchwrapper 体系）
void do_legacy_forge_install(ConfigState *cstate, const char *mc_version,
                             const char *vanilla_url, const char *custom_name,
                             const char *modloader, const char *type,
                             const ForgeConfig *fcfg,
                             const char *forge_version);
```

- [ ] **Step 2: Add `is_legacy_forge()` helper to install.c**

In `src/version/install.c`, add after the forward-declaration block (after the `get_jar_main_class` forward-decl near line 57, before `pick_modloader`):

```c
// 判断 MC 版本是否为旧版 Forge（minor ≤ 12）
static int is_legacy_forge(const char *mc_version) {
  if (!mc_version) return 0;
  const char *dot1 = strchr(mc_version, '.');
  if (!dot1) return 0;
  const char *dot2 = strchr(dot1 + 1, '.');
  size_t len = dot2 ? (size_t)(dot2 - dot1 - 1) : strlen(dot1 + 1);
  if (len == 0 || len > 4) return 0;
  char minor[8];
  memcpy(minor, dot1 + 1, len);
  minor[len] = '\0';
  return atoi(minor) <= 12;
}
```

- [ ] **Step 3: Build and verify**

```bash
make clean && make
```
Expected: Compiles successfully. Unused function warnings OK.

---

### Task 2: Modify `install_page()` to route old Forge to `do_legacy_forge_install()`

**Files:**
- Modify: `src/version/install.c:260-269` (the forge/neoforge dispatch block in `install_page`)

**Interfaces:**
- Consumes: `is_legacy_forge()` from Task 1
- Produces: Routing logic that dispatches to `do_legacy_forge_install()` for old Forge

- [ ] **Step 1: Modify the forge/neoforge dispatch block**

In `install_page()`, find the block at lines ~260-269:
```c
          if (modloader_sel >= 2) { // forge / neoforge
            const ForgeConfig *fcfg =
                (modloader_sel == 2) ? &FORGE_CFG : &NEOFORGE_CFG;
            do_forge_install(cstate, selected_version, selected_url,
                             user_name, modloaders[modloader_sel],
                             types[s.type_sel], fcfg, forge_ver);
            goto quit;
```

Replace the `do_forge_install` call with a routed dispatch:
```c
          if (modloader_sel >= 2) { // forge / neoforge
            const ForgeConfig *fcfg =
                (modloader_sel == 2) ? &FORGE_CFG : &NEOFORGE_CFG;
            if (modloader_sel == 2 && is_legacy_forge(selected_version)) {
              do_legacy_forge_install(cstate, selected_version, selected_url,
                                      user_name, modloaders[modloader_sel],
                                      types[s.type_sel], fcfg, forge_ver);
            } else {
              do_forge_install(cstate, selected_version, selected_url,
                               user_name, modloaders[modloader_sel],
                               types[s.type_sel], fcfg, forge_ver);
            }
            goto quit;
```

- [ ] **Step 2: Build — expect linker error**

```bash
make clean && make
```
Expected: linker error — `undefined reference to 'do_legacy_forge_install'`. Confirms routing is wired; function not yet implemented.

---

### Task 3: Implement `do_legacy_forge_install()` — setup + Phase 1 (installer) + Phase 2 (vanilla JSON)

**Files:**
- Modify: `src/version/install.c` — add function before the `// ======================== Forge/NeoForge 安装主函数 ========================` comment

**Interfaces:**
- Consumes: `dl_one()` (install.c:488), `cJSON` API, `snprintf`, `access`, `mkdir`, `system`, `popen`
- Produces: `cJSON *install_profile`, `cJSON *version_info`, confirmed vanilla JSON on disk

- [ ] **Step 1: Add the function with Phases 0-2**

Add this function before the `// ======================== Forge/NeoForge 安装主函数` comment block:

```c
// ======================== 旧版 Forge 安装主函数（1.7.10–1.12.2） ========================
void do_legacy_forge_install(ConfigState *cstate, const char *mc_version,
                             const char *vanilla_url, const char *custom_name,
                             const char *modloader, const char *type,
                             const ForgeConfig *fcfg,
                             const char *forge_version) {
  struct sigaction sa = {.sa_handler = on_sigint, .sa_flags = 0};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_cancel = 0;
  endwin();

  int num_threads = atoi(cstate->items[6].value);
  if (num_threads < 1) num_threads = 96;
  char *mcdir = cstate->items[2].value;

  printf("\n=== Installing %s (%s / forge legacy) ===\n", custom_name,
         mc_version);
  printf("    Threads: 8 (libs) / %d (assets)\n\n", num_threads);

  // 创建版本目录
  char version_dir[512];
  snprintf(version_dir, sizeof(version_dir), "%s/versions/%s", mcdir,
           custom_name);
  mkdir(version_dir, 0755);

  char json_path[600];
  snprintf(json_path, sizeof(json_path), "%s/%s.json", version_dir,
           custom_name);

  // ---- Phase 0: 确认 forge 版本（已在 install_page 中预选） ----
  char *chosen_forge_ver = NULL;
  if (forge_version && forge_version[0]) {
    chosen_forge_ver = strdup(forge_version);
  }
  if (!chosen_forge_ver) {
    printf("ERROR: No forge version specified.\n");
    goto done;
  }
  printf("  Forge version: %s (legacy)\n", chosen_forge_ver);

  // 构建 Maven 路径
  char group_path[256];
  snprintf(group_path, sizeof(group_path), "%s", fcfg->group_id);
  for (char *p = group_path; *p; p++)
    if (*p == '.') *p = '/';

  // ---- Phase 1: 下载 installer jar 并提取 install_profile.json ----
  char installer_url[1024];
  snprintf(installer_url, sizeof(installer_url),
           "https://%s/%s/%s/%s/%s-%s-installer.jar",
           fcfg->maven_base, group_path, fcfg->artifact_id,
           chosen_forge_ver, fcfg->artifact_id, chosen_forge_ver);

  char installer_path[600];
  snprintf(installer_path, sizeof(installer_path),
           "/tmp/tmcl_forge_legacy_installer_%s.jar", chosen_forge_ver);
  printf("  Downloading installer...\n");
  if (dl_one(installer_url, installer_path, "installer jar") != 0) {
    printf("ERROR: Failed to download installer jar.\n");
    goto done;
  }

  // 提取 install_profile.json
  printf("  Extracting install_profile.json...\n");
  char extract_cmd[1536];
  snprintf(extract_cmd, sizeof(extract_cmd),
           "unzip -p '%s' install_profile.json 2>/dev/null", installer_path);
  FILE *ep = popen(extract_cmd, "r");
  if (!ep) {
    printf("ERROR: Failed to extract install_profile.json.\n");
    remove(installer_path);
    goto done;
  }

  char *profile_raw = NULL;
  size_t pcap = 0, plen = 0;
  {
    char chunk[4096];
    while (fgets(chunk, sizeof(chunk), ep)) {
      size_t cl = strlen(chunk);
      if (plen + cl + 1 > pcap) {
        pcap = pcap ? pcap * 2 : 16384;
        void *newbuf = realloc(profile_raw, pcap);
        if (!newbuf) { pclose(ep); free(profile_raw); goto done; }
        profile_raw = newbuf;
      }
      memcpy(profile_raw + plen, chunk, cl);
      plen += cl;
      profile_raw[plen] = '\0';
    }
  }
  pclose(ep);
  remove(installer_path);

  if (!profile_raw) {
    printf("ERROR: install_profile.json is empty.\n");
    goto done;
  }

  // parse install_profile
  cJSON *install_profile = cJSON_Parse(profile_raw);
  free(profile_raw);
  if (!install_profile) {
    printf("ERROR: Failed to parse install_profile.json.\n");
    goto done;
  }

  // extract versionInfo
  cJSON *version_info = cJSON_GetObjectItem(install_profile, "versionInfo");
  if (!version_info) {
    printf("ERROR: install_profile.json missing 'versionInfo'.\n");
    cJSON_Delete(install_profile);
    goto done;
  }

  // ---- Phase 2: 确保原版 JSON 存在 ----
  char vanilla_json_path[600];
  snprintf(vanilla_json_path, sizeof(vanilla_json_path),
           "%s/versions/%s/%s.json", mcdir, mc_version, mc_version);

  if (access(vanilla_json_path, R_OK) != 0) {
    printf("  Vanilla JSON not found, downloading %s...\n", mc_version);
    char tmp_vanilla[600];
    snprintf(tmp_vanilla, sizeof(tmp_vanilla),
             "/tmp/tmcl_vanilla_%s.json", mc_version);
    if (dl_one(vanilla_url, tmp_vanilla, "vanilla JSON") != 0) {
      printf("ERROR: Failed to download vanilla JSON.\n");
      cJSON_Delete(install_profile);
      goto done;
    }
    char vanilla_dir[512];
    snprintf(vanilla_dir, sizeof(vanilla_dir), "%s/versions/%s", mcdir,
             mc_version);
    mkdir(vanilla_dir, 0755);
    char cp_cmd[1280];
    snprintf(cp_cmd, sizeof(cp_cmd), "cp '%s' '%s'", tmp_vanilla,
             vanilla_json_path);
    if (system(cp_cmd) != 0) {
      printf("ERROR: Failed to copy vanilla JSON.\n");
      remove(tmp_vanilla);
      cJSON_Delete(install_profile);
      goto done;
    }
    remove(tmp_vanilla);
    printf("  Vanilla JSON saved.\n");
  } else {
    printf("  Vanilla JSON already exists.\n");
  }

  // ---- Phase 3: 收集并下载所有文件 ----
  printf("  Reading vanilla JSON for library list...\n");
  FILE *vf = fopen(vanilla_json_path, "r");
  if (!vf) { cJSON_Delete(install_profile); goto done; }
  fseek(vf, 0, SEEK_END);
  long vsz = ftell(vf);
  rewind(vf);
  char *vdata = malloc(vsz + 1);
  if (!vdata) { fclose(vf); cJSON_Delete(install_profile); goto done; }
  fread(vdata, 1, vsz, vf);
  fclose(vf);
  vdata[vsz] = '\0';

  cJSON *vanilla = cJSON_Parse(vdata);
  free(vdata);
  if (!vanilla) {
    printf("ERROR: Failed to parse vanilla JSON.\n");
    cJSON_Delete(install_profile);
    goto done;
  }

  // 构建下载任务数组
  DlTask *tasks = malloc(8192 * sizeof(DlTask));
  if (!tasks) { cJSON_Delete(vanilla); cJSON_Delete(install_profile); goto done; }
  int tcount = 0;

  // a) 原版 libraries（从 vanilla JSON）
  cJSON *v_libs = cJSON_GetObjectItem(vanilla, "libraries");
  if (v_libs && cJSON_IsArray(v_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, v_libs) {
      if (!lib_rules_ok(cJSON_GetObjectItem(lib, "rules"))) continue;
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) tcount++;
    }
  }
  int vanilla_lib_count = tcount;

  // b) Forge libraries（从 install_profile 的 versionInfo.libraries）
  cJSON *vi_libs = cJSON_GetObjectItem(version_info, "libraries");
  if (vi_libs && cJSON_IsArray(vi_libs)) {
    cJSON *lib;
    cJSON_ArrayForEach(lib, vi_libs) {
      DlTask *t = &tasks[tcount];
      if (build_lib_task(t, lib, mcdir)) {
        char lbl[300];
        snprintf(lbl, sizeof(lbl), "[forge] %s", t->label);
        snprintf(t->label, sizeof(t->label), "%s", lbl);
        tcount++;
      }
    }
  }
  int forge_lib_count = tcount - vanilla_lib_count;
  printf("  Libraries: %d vanilla + %d forge = %d total\n", vanilla_lib_count,
         forge_lib_count, tcount);

  // c) 原版 client.jar（放到 inheritsFrom 指定的原版目录）
  {
    char client_jar_path[600];
    snprintf(client_jar_path, sizeof(client_jar_path),
             "%s/versions/%s/%s.jar", mcdir, mc_version, mc_version);
    cJSON *downloads = cJSON_GetObjectItem(vanilla, "downloads");
    if (downloads) {
      cJSON *client = cJSON_GetObjectItem(downloads, "client");
      if (client) {
        cJSON *jar_url = cJSON_GetObjectItem(client, "url");
        if (jar_url && cJSON_IsString(jar_url)) {
          snprintf(tasks[tcount].url, sizeof(tasks[tcount].url), "%s",
                   jar_url->valuestring);
          snprintf(tasks[tcount].dest, sizeof(tasks[tcount].dest), "%s",
                   client_jar_path);
          snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                   "vanilla client.jar");
          tcount++;
        }
      }
    }
  }

  // d) Asset index
  char ai_url[1024] = {0};
  char ai_path[512] = {0};
  {
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
        snprintf(tasks[tcount].label, sizeof(tasks[tcount].label),
                 "asset index");
        tcount++;
      }
    }
  }

  // ---- Phase 3b: 并行下载 ----
  printf("  Downloading %d files...\n\n", tcount);
  dl_parallel(tasks, tcount, 8);

  // 重试失败
  char **failed_names = NULL;
  int failed_count = 0;
  if (g_cancel) {
    printf("\n  Cancelled by user, skipping retries.\n");
    goto clean_tasks;
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

  // ---- Phase 4: 生成 forge 版本 JSON ----
  if (!g_cancel) {
    printf("\n  Writing version JSON...\n");
    cJSON_ReplaceItemInObject(version_info, "id",
                              cJSON_CreateString(custom_name));
    cJSON *forge_meta = cJSON_CreateObject();
    cJSON_AddStringToObject(forge_meta, "version", chosen_forge_ver);
    cJSON_AddItemToObject(version_info, "forge", forge_meta);

    char *json_str = cJSON_Print(version_info);
    if (json_str) {
      FILE *jf = fopen(json_path, "w");
      if (jf) {
        fprintf(jf, "%s", json_str);
        fclose(jf);
      }
      free(json_str);
    }
  }

  // ---- Phase 5: 下载 assets ----
  if (ai_path[0] && !g_cancel) {
    FILE *aif = fopen(ai_path, "r");
    if (aif) {
      fseek(aif, 0, SEEK_END);
      long asz = ftell(aif);
      rewind(aif);
      char *ai_data = malloc(asz + 1);
      if (ai_data) { fread(ai_data, 1, asz, aif); ai_data[asz] = '\0'; }
      fclose(aif);

      cJSON *ai_root = ai_data ? cJSON_Parse(ai_data) : NULL;
      free(ai_data);
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
          if (atasks) {
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
                       "https://resources.download.minecraft.net/%s/%s",
                       first2, hash);
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
                  if (failed_names) {
                    for (int i = 0; i < a_fc; i++)
                      failed_names[failed_count++] = a_fail[i];
                  }
                  free(a_fail);
                }
              }
            } else {
              printf("\n  Assets: all %d already cached\n", already);
            }
            free(atasks);
          }
        }
        cJSON_Delete(ai_root);
      }
    }
  }

clean_tasks:
  cJSON_Delete(vanilla);
  free(tasks);

  // 打印失败总结
  if (failed_count > 0 && !g_cancel) {
    printf("\n=== %d file(s) failed to download ===\n\n", failed_count);
    for (int i = 0; i < failed_count; i++) {
      printf("  FAILED: %s\n", failed_names[i]);
      free(failed_names[i]);
    }
    free(failed_names);
  }

  printf("\n=== Installation complete: %s ===\n", custom_name);
  if (g_cancel) printf("  (cancelled by user)\n");
  printf("Press Enter to return...\n");
  while (getchar() != '\n') {}

done:
  cJSON_Delete(install_profile);
  free(chosen_forge_ver);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGINT, &sa, NULL);
}
```

- [ ] **Step 2: Build**

```bash
make clean && make
```
Expected: Compiles successfully. Fix any warnings.

---

### Task 4: Build, test, and commit

**Files:**
- Modify: `src/version/install.c` — any final cleanup/fixes from build warnings

- [ ] **Step 1: Clean build with -Wall**

```bash
make clean && make
```
Expected: Zero warnings, zero errors.

- [ ] **Step 2: Functional test — install old Forge 1.12.2**

```bash
./tmcl
```

Manual test flow:
1. Navigate to Version page (press `V`)
2. Press `n` for new version
3. Select type `release`
4. Select MC version `1.12.2`
5. Select mod loader `forge`
6. Observe version selector with available forge 1.12.2 versions
7. Select a version (e.g., `1.12.2-14.23.5.2859`)
8. Enter a name or use auto-generated
9. Press Enter on `[ start installation ]`
10. Observe: installer downloaded → vanilla JSON downloaded → libs downloaded → JSON written → assets downloaded
11. Verify: version appears in version list with modloader `forge`

- [ ] **Step 3: Verify JSON output**

```bash
cat ~/.minecraft/versions/<custom_name>/<custom_name>.json | python3 -m json.tool | head -30
```
Expected: Has `inheritsFrom: "1.12.2"`, `mainClass: "net.minecraft.launchwrapper.Launch"`, `minecraftArguments`, `forge: {"version": "..."}`. No `processors` field.

- [ ] **Step 4: Launch the old Forge version**

Select the installed forge version in the version list, press Enter. Verify it launches successfully (launchwrapper output, FML tweak class loading).

- [ ] **Step 5: Commit**

```bash
git add src/version/install.c src/version/install.h
git commit -m "feat: legacy forge (1.7.10-1.12.2) installation support

- Add is_legacy_forge() helper for MC version detection
- Route old Forge to do_legacy_forge_install() in install_page
- Implement do_legacy_forge_install() with inheritsFrom-based flow
- Auto-download vanilla JSON + libraries when missing"
```
