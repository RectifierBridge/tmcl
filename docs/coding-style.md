# 编码规范

## 命名

| 类型 | 规范 | 示例 |
|------|------|------|
| 函数 | `snake_case` | `init_versions()`, `begin_version()` |
| 结构体 | `PascalCase` | `VersionInfo`, `ConfigState` |
| 宏 | `UPPER_SNAKE` | `MANIFEST_URL`, `MAX_VERSIONS` |
| 变量 | `snake_case` | `selected_version`, `scroll_offset` |
| 静态函数 | `snake_case` 或简写 | `sub_arg()`, `dl_one()`, `rules_ok()` |

## 格式

- 缩进: **2 空格**
- 大括号: K&R 风格 (开括号同行)
- 注释: **中文**，`//` 行注释或 `/* */` 块注释

## 安全

1. **缓冲区**: 始终用 `snprintf(buf, sizeof(buf), ...)` 而非 `sprintf`
2. **内存**: `malloc` / `strdup` 必须对应 `free`；安装任务使用 `tofree` 数组统一释放
3. **字符串**: `strncpy(dst, src, size-1)` + 手动 `dst[size-1] = '\0'`
4. **数组边界**: 参数数组有明确 MAX 上限，添加前检查 `ac < max_ac - 1`

## 模式

### ncurses 页面
```c
void xxx_page(int ch, int *middlep, XxxState *state, ...) {
  clear();
  int row, col;
  getmaxyx(stdscr, row, col);
  // 处理按键
  switch (ch) { ... }
  // 绘制 UI
  mvprintw(...);
}
```

### 弹出选择器
```c
while (1) {
  clear();
  // 绘制选项列表，高亮当前选中
  // j/k 移动，Enter 确认，q 取消
  int c = getch();
  if (c == '\n') break;
  if (c == 'q') { sel = -1; break; }
}
```

### JSON 读取
```c
FILE *f = fopen(path, "r");
fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
char *data = malloc(sz + 1);
fread(data, 1, sz, f); data[sz] = '\0'; fclose(f);
cJSON *root = cJSON_Parse(data); free(data);
// 使用 root...
cJSON_Delete(root);
```
