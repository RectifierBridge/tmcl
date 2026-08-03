#include "mod_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 内部辅助 ====================

// 安全字符串复制（自动截断）
static void safe_copy(char *dst, size_t dst_size, const char *src) {
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= dst_size)
    len = dst_size - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// cJSON 安全取字符串，key 不存在返回 ""
static const char *json_str(cJSON *obj, const char *key) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsString(item))
    return item->valuestring;
  return "";
}

// cJSON 安全取整数，key 不存在返回 0
static int json_int(cJSON *obj, const char *key) {
  cJSON *item = cJSON_GetObjectItem(obj, key);
  if (cJSON_IsNumber(item))
    return item->valueint;
  return 0;
}

// ==================== Modrinth 搜索解析 → ModResult[] ====================
static int parse_mr_search(cJSON *root, ModResult **out) {
  cJSON *hits = cJSON_GetObjectItem(root, "hits");
  if (!cJSON_IsArray(hits))
    return 0;

  int count = cJSON_GetArraySize(hits);
  if (count == 0)
    return 0;

  *out = malloc(count * sizeof(ModResult));
  if (!*out)
    return 0;

  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(hits, i);
    ModResult *r = &(*out)[i];
    memset(r, 0, sizeof(ModResult));
    safe_copy(r->id, sizeof(r->id), json_str(item, "project_id"));
    safe_copy(r->title, sizeof(r->title), json_str(item, "title"));
    safe_copy(r->description, sizeof(r->description),
              json_str(item, "description"));
    r->downloads = json_int(item, "downloads");
    r->source = SOURCE_MODRINTH;
  }
  return count;
}

// ==================== CurseForge 搜索解析 → ModResult[] ====================
static int parse_cf_search(cJSON *root, ModResult **out) {
  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsArray(data))
    return 0;

  int count = cJSON_GetArraySize(data);
  if (count == 0)
    return 0;

  *out = malloc(count * sizeof(ModResult));
  if (!*out)
    return 0;

  for (int i = 0; i < count; i++) {
    cJSON *item = cJSON_GetArrayItem(data, i);
    ModResult *r = &(*out)[i];
    memset(r, 0, sizeof(ModResult));
    cJSON *id_obj = cJSON_GetObjectItem(item, "id");
    if (cJSON_IsNumber(id_obj))
      snprintf(r->id, sizeof(r->id), "%d", id_obj->valueint);
    safe_copy(r->title, sizeof(r->title), json_str(item, "name"));
    safe_copy(r->description, sizeof(r->description),
              json_str(item, "summary"));
    r->downloads = json_int(item, "downloadCount");
    r->source = SOURCE_CURSEFORGE;
  }
  return count;
}

// ==================== Modrinth 详情解析 → ModDetail ====================
static bool parse_mr_detail(cJSON *root, ModDetail *out) {
  memset(out, 0, sizeof(ModDetail));
  safe_copy(out->id, sizeof(out->id), json_str(root, "id"));
  safe_copy(out->title, sizeof(out->title), json_str(root, "title"));
  safe_copy(out->description, sizeof(out->description),
            json_str(root, "description"));
  out->downloads = json_int(root, "downloads");
  out->followers = json_int(root, "followers");

  const char *body = json_str(root, "body");
  safe_copy(out->body, sizeof(out->body), body);

  safe_copy(out->client_side, sizeof(out->client_side),
            json_str(root, "client_side"));
  safe_copy(out->server_side, sizeof(out->server_side),
            json_str(root, "server_side"));

  // 拼接 loaders
  cJSON *loaders = cJSON_GetObjectItem(root, "loaders");
  if (cJSON_IsArray(loaders)) {
    int n = cJSON_GetArraySize(loaders);
    int pos = 0;
    for (int i = 0; i < n; i++) {
      cJSON *l = cJSON_GetArrayItem(loaders, i);
      if (cJSON_IsString(l)) {
        pos += snprintf(out->loaders_list + pos,
                        sizeof(out->loaders_list) - pos, "%s%s",
                        i > 0 ? ", " : "", l->valuestring);
        if ((size_t)pos >= sizeof(out->loaders_list))
          break;
      }
    }
  }
  out->source = SOURCE_MODRINTH;
  return true;
}

// ==================== CurseForge 详情解析 → ModDetail ====================
static bool parse_cf_detail(cJSON *root, ModDetail *out) {
  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data))
    return false;

  memset(out, 0, sizeof(ModDetail));
  cJSON *id_obj = cJSON_GetObjectItem(data, "id");
  if (cJSON_IsNumber(id_obj))
    snprintf(out->id, sizeof(out->id), "%d", id_obj->valueint);
  safe_copy(out->title, sizeof(out->title), json_str(data, "name"));
  safe_copy(out->description, sizeof(out->description),
            json_str(data, "summary"));
  out->downloads = json_int(data, "downloadCount");
  out->followers = 0; // CurseForge 无此字段

  // body 从 description 字段读取（但 CurseForge /v1/mods/{id} 不返回 description，
  // 需要额外调用 /v1/mods/{id}/description，由调用者在 action_install 中处理）
  safe_copy(out->body, sizeof(out->body),
            json_str(data, "description"));
  safe_copy(out->client_side, sizeof(out->client_side), "required");
  safe_copy(out->server_side, sizeof(out->server_side), "required");

  // 从 latestFiles[0].gameVersions 提取 mod loader（非 categories）
  cJSON *latest_files = cJSON_GetObjectItem(data, "latestFiles");
  if (cJSON_IsArray(latest_files)) {
    cJSON *first_file = cJSON_GetArrayItem(latest_files, 0);
    if (cJSON_IsObject(first_file)) {
      cJSON *game_versions = cJSON_GetObjectItem(first_file, "gameVersions");
      if (cJSON_IsArray(game_versions)) {
        int n = cJSON_GetArraySize(game_versions);
        int pos = 0;
        for (int i = 0; i < n; i++) {
          cJSON *gv = cJSON_GetArrayItem(game_versions, i);
          if (cJSON_IsString(gv)) {
            const char *v = gv->valuestring;
            // 检查是否为已知 mod loader，输出小写
            const char *lc = NULL;
            if (strcmp(v, "Forge") == 0) lc = "forge";
            else if (strcmp(v, "Fabric") == 0) lc = "fabric";
            else if (strcmp(v, "NeoForge") == 0) lc = "neoforge";
            else if (strcmp(v, "Quilt") == 0) lc = "quilt";
            if (lc) {
              pos += snprintf(out->loaders_list + pos,
                              sizeof(out->loaders_list) - pos, "%s%s",
                              pos > 0 ? ", " : "", lc);
              if ((size_t)pos >= sizeof(out->loaders_list))
                break;
            }
          }
        }
      }
    }
  }
  out->source = SOURCE_CURSEFORGE;
  return true;
}

// ==================== Modrinth 版本解析 → ModVersion[]（带过滤）====================
static int parse_mr_versions(cJSON *root, const char *mc_version,
                             const char *modloader, ModVersion **out) {
  if (!cJSON_IsArray(root))
    return 0;

  int total = cJSON_GetArraySize(root);
  if (total == 0)
    return 0;

  // 先分配最大可能数量
  ModVersion *temp = malloc(total * sizeof(ModVersion));
  if (!temp)
    return 0;

  int count = 0;
  for (int i = 0; i < total; i++) {
    cJSON *ver = cJSON_GetArrayItem(root, i);

    // 过滤 MC 版本
    int mc_ok = (mc_version[0] == '\0'); // 空字符串表示不过滤
    cJSON *game_versions = cJSON_GetObjectItem(ver, "game_versions");
    if (!mc_ok && cJSON_IsArray(game_versions)) {
      int gv_size = cJSON_GetArraySize(game_versions);
      for (int j = 0; j < gv_size; j++) {
        cJSON *gv = cJSON_GetArrayItem(game_versions, j);
        if (cJSON_IsString(gv) && strcmp(mc_version, gv->valuestring) == 0) {
          mc_ok = 1;
          break;
        }
      }
    }
    if (!mc_ok)
      continue;

    // 过滤 modloader
    int loader_ok = (modloader[0] == '\0'); // 空字符串表示不过滤
    cJSON *loaders = cJSON_GetObjectItem(ver, "loaders");
    if (!loader_ok && cJSON_IsArray(loaders)) {
      int l_size = cJSON_GetArraySize(loaders);
      for (int j = 0; j < l_size; j++) {
        cJSON *l = cJSON_GetArrayItem(loaders, j);
        if (cJSON_IsString(l) && strcmp(modloader, l->valuestring) == 0) {
          loader_ok = 1;
          break;
        }
      }
    }
    if (!loader_ok)
      continue;

    // 填充 ModVersion
    ModVersion *mv = &temp[count];
    memset(mv, 0, sizeof(ModVersion));
    safe_copy(mv->version_id, sizeof(mv->version_id),
              json_str(ver, "id"));

    cJSON *files = cJSON_GetObjectItem(ver, "files");
    if (cJSON_IsArray(files)) {
      cJSON *file0 = cJSON_GetArrayItem(files, 0);
      safe_copy(mv->filename, sizeof(mv->filename), json_str(file0, "filename"));
      safe_copy(mv->download_url, sizeof(mv->download_url),
                json_str(file0, "url"));
    }

    safe_copy(mv->version_number, sizeof(mv->version_number),
              json_str(ver, "version_number"));

    const char *vtype = json_str(ver, "version_type");
    if (strcmp(vtype, "release") == 0)
      mv->release_type = 0;
    else if (strcmp(vtype, "beta") == 0)
      mv->release_type = 1;
    else if (strcmp(vtype, "alpha") == 0)
      mv->release_type = 2;
    else
      mv->release_type = 0;

    mv->downloads = json_int(ver, "downloads");

    // 拼接 game_versions
    if (cJSON_IsArray(game_versions)) {
      int n = cJSON_GetArraySize(game_versions);
      int pos = 0;
      for (int j = 0; j < n; j++) {
        cJSON *gv = cJSON_GetArrayItem(game_versions, j);
        if (cJSON_IsString(gv)) {
          pos += snprintf(mv->game_versions + pos,
                          sizeof(mv->game_versions) - pos, "%s%s",
                          j > 0 ? " " : "", gv->valuestring);
          if ((size_t)pos >= sizeof(mv->game_versions))
            break;
        }
      }
    }

    // 拼接 loaders
    if (cJSON_IsArray(loaders)) {
      int n = cJSON_GetArraySize(loaders);
      int pos = 0;
      for (int j = 0; j < n; j++) {
        cJSON *l = cJSON_GetArrayItem(loaders, j);
        if (cJSON_IsString(l)) {
          pos += snprintf(mv->loaders + pos, sizeof(mv->loaders) - pos,
                          "%s%s", j > 0 ? " " : "", l->valuestring);
          if ((size_t)pos >= sizeof(mv->loaders))
            break;
        }
      }
    }

    mv->source = SOURCE_MODRINTH;
    count++;
  }

  *out = temp;
  return count;
}

// ==================== CurseForge 版本解析 → ModVersion[] ====================
static int parse_cf_versions(cJSON *root, ModVersion **out) {
  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsArray(data))
    return 0;

  int total = cJSON_GetArraySize(data);
  if (total == 0)
    return 0;

  *out = malloc(total * sizeof(ModVersion));
  if (!*out)
    return 0;

  int count = 0;
  for (int i = 0; i < total; i++) {
    cJSON *file = cJSON_GetArrayItem(data, i);
    ModVersion *mv = &(*out)[count];
    memset(mv, 0, sizeof(ModVersion));

    // ID
    cJSON *id_obj = cJSON_GetObjectItem(file, "id");
    if (cJSON_IsNumber(id_obj))
      snprintf(mv->version_id, sizeof(mv->version_id), "%d", id_obj->valueint);

    // 文件名和显示名
    safe_copy(mv->filename, sizeof(mv->filename),
              json_str(file, "fileName"));
    safe_copy(mv->version_number, sizeof(mv->version_number),
              json_str(file, "displayName"));
    // 如果 displayName 为空，用 fileName
    if (mv->version_number[0] == '\0')
      safe_copy(mv->version_number, sizeof(mv->version_number),
                json_str(file, "fileName"));

    // 下载 URL：优先 downloadUrl，fallback 构造
    const char *durl = json_str(file, "downloadUrl");
    if (durl[0] != '\0') {
      safe_copy(mv->download_url, sizeof(mv->download_url), durl);
    } else {
      // Fallback: 使用 file id 构造 URL
      int fid = json_int(file, "id");
      if (fid > 0) {
        snprintf(mv->download_url, sizeof(mv->download_url),
                 "https://edge.forgecdn.net/files/%d/%d/%s",
                 fid / 1000, fid % 1000, mv->filename);
      }
    }

    // releaseType: 1=Release, 2=Beta, 3=Alpha
    mv->release_type = curseforge_release_type(json_int(file, "releaseType"));

    mv->downloads = json_int(file, "downloadCount");

    // gameVersions 数组 → 空格分隔
    cJSON *game_versions = cJSON_GetObjectItem(file, "gameVersions");
    if (cJSON_IsArray(game_versions)) {
      int n = cJSON_GetArraySize(game_versions);
      int pos = 0;
      for (int j = 0; j < n; j++) {
        cJSON *gv = cJSON_GetArrayItem(game_versions, j);
        if (cJSON_IsString(gv)) {
          pos += snprintf(mv->game_versions + pos,
                          sizeof(mv->game_versions) - pos, "%s%s",
                          j > 0 ? " " : "", gv->valuestring);
          if ((size_t)pos >= sizeof(mv->game_versions))
            break;
        }
      }
    }

    // CF 文件级别无 loaders 字段，填入 "Forge" 占位
    safe_copy(mv->loaders, sizeof(mv->loaders), "Forge");

    mv->source = SOURCE_CURSEFORGE;
    count++;
  }

  return count;
}

// ==================== Public API ====================

int mod_api_parse_search(cJSON *root, int source, ModResult **out) {
  if (!root || !out)
    return 0;
  if (source == SOURCE_MODRINTH)
    return parse_mr_search(root, out);
  else if (source == SOURCE_CURSEFORGE)
    return parse_cf_search(root, out);
  return 0;
}

bool mod_api_parse_detail(cJSON *root, int source, ModDetail *out) {
  if (!root || !out)
    return false;
  if (source == SOURCE_MODRINTH)
    return parse_mr_detail(root, out);
  else if (source == SOURCE_CURSEFORGE)
    return parse_cf_detail(root, out);
  return false;
}

int mod_api_parse_versions(cJSON *root, int source,
                           const char *mc_version, const char *modloader,
                           ModVersion **out) {
  if (!root || !out)
    return 0;
  if (source == SOURCE_MODRINTH)
    return parse_mr_versions(root, mc_version, modloader, out);
  else if (source == SOURCE_CURSEFORGE)
    return parse_cf_versions(root, out);
  return 0;
}

int curseforge_loader_id(const char *loader) {
  if (!loader)
    return 0;
  if (strcmp(loader, "forge") == 0)
    return 1;
  if (strcmp(loader, "fabric") == 0)
    return 4;
  if (strcmp(loader, "neoforge") == 0)
    return 6;
  if (strcmp(loader, "quilt") == 0)
    return 5;
  return 0; // 0 = Any
}

int curseforge_release_type(int cf_type) {
  // CF: 1=Release, 2=Beta, 3=Alpha
  // 统一: 0=Release, 1=Beta, 2=Alpha
  if (cf_type == 2)
    return 1;
  if (cf_type == 3)
    return 2;
  return 0;
}
