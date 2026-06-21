#!/bin/bash
# count_lines.sh — 统计项目中的代码行数
# 默认扫描当前目录，也可指定目录：./count_lines.sh /path/to/project

PROJECT_DIR="${1:-.}"

# 需要统计的文件名模式（可根据实际项目调整）
PATTERNS=(
    "*.c"
    "*.h"
    "*.sh"
    "Makefile"
)

# ---------- 构建 find 的 -name 条件 ----------
FIND_COND=()
first=true
for p in "${PATTERNS[@]}"; do
    if $first; then
        FIND_COND+=(-name "$p")
        first=false
    else
        FIND_COND+=( -o -name "$p")
    fi
done

# ---------- 统计 ----------
total=0
count=0
while IFS= read -r -d '' file; do
    lines=$(wc -l < "$file")
    total=$((total + lines))
    count=$((count + 1))
    printf "%6d  %s\n" "$lines" "$file"
done < <(find "$PROJECT_DIR" -type f \( "${FIND_COND[@]}" \) -print0 2>/dev/null)

echo "------------------------------"
echo "文件数: $count"
echo "总行数: $total"
