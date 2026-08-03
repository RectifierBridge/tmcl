#ifndef MOD_API_H
#define MOD_API_H

#include <cjson/cJSON.h>
#include <stdbool.h>

// 模组源标识
#define SOURCE_MODRINTH   0
#define SOURCE_CURSEFORGE 1

// CurseForge API Key（来自 HMCL 社区 key）
#define CURSEFORGE_API_KEY "$2a$10$o8pygPrhvKBHuuh5imL2W.LCNFhB15zBYAExXx/TqTx/Zp5px2lxu"

// ---------- 统一搜索结果 ----------
typedef struct {
  char id[64];           // Modrinth: project_id, CurseForge: 数字 id 字符串
  char title[128];
  char description[512];
  int downloads;
  int source;            // SOURCE_MODRINTH / SOURCE_CURSEFORGE
} ModResult;

// ---------- 统一详情 ----------
typedef struct {
  char id[64];
  char title[128];
  char description[512];
  int downloads;
  int followers;         // CurseForge 无此字段，填 0
  char body[16384];      // 正文（截断到 16384）
  char client_side[32];
  char server_side[32];
  char loaders_list[256]; // 逗号分隔，如 "fabric,forge,neoforge"
  int source;
} ModDetail;

// ---------- 统一版本（可下载文件）----------
typedef struct {
  char version_id[64];     // Modrinth version id / CurseForge file id
  char filename[256];      // 下载文件名
  char download_url[1024]; // 已解析好的可直接下载的 URL
  char version_number[64]; // 显示的版本号
  int release_type;        // 0=release, 1=beta, 2=alpha
  int downloads;
  char game_versions[512]; // 支持的 MC 版本，空格分隔
  char loaders[256];       // 支持的 modloader，空格分隔
  int source;
} ModVersion;

// ---------- 解析函数 ----------

// 搜索 JSON → ModResult 数组，返回数量，调用者负责 free(*out)
int mod_api_parse_search(cJSON *root, int source, ModResult **out);

// 详情 JSON → ModDetail，失败返回 false
bool mod_api_parse_detail(cJSON *root, int source, ModDetail *out);

// 版本 JSON → ModVersion 数组，同时完成 MC 版本+modloader 过滤，返回数量
// CurseForge: 过滤由 API 参数完成，此函数只负责解析
// Modrinth: 过滤在本函数内完成
int mod_api_parse_versions(cJSON *root, int source,
                           const char *mc_version, const char *modloader,
                           ModVersion **out);

// CurseForge modloader 名 → API 数字 (fabric→4, forge→1, neoforge→6, quilt→5)
int curseforge_loader_id(const char *loader);

// CurseForge releaseType (1/2/3) → 统一 release_type (0/1/2)
int curseforge_release_type(int cf_type);

#endif
