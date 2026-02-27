#!/usr/bin/env bash
# check_duplicates.sh — 检查发布目录中同名组件多副本
set -euo pipefail

PACKAGE_DIR="${1:?Usage: check_duplicates.sh <package_dir>}"
ERRORS=0

# 规则 1: bin/ 下不应存在 stdio.drv.* 可执行文件
BIN_DIR="${PACKAGE_DIR}/bin"
if [[ -d "${BIN_DIR}" ]]; then
    while IFS= read -r -d '' f; do
        base="$(basename "$f")"
        echo "ERROR: driver in bin/: ${base} (should be in data_root/drivers/ only)"
        ERRORS=$((ERRORS + 1))
    done < <(find "${BIN_DIR}" -maxdepth 1 -type f -name "stdio.drv.*" -print0 2>/dev/null)
fi

# 规则 2: data_root/drivers/ 下同名 driver 不应出现在多个子目录
DRIVERS_DIR="${PACKAGE_DIR}/data_root/drivers"
if [[ -d "${DRIVERS_DIR}" ]]; then
    while IFS= read -r dup; do
        locations=$(find "${DRIVERS_DIR}" -type f -name "${dup}" -exec dirname {} \; | \
            xargs -I{} basename {} | paste -sd ", ")
        echo "ERROR: duplicate driver '${dup}' in: ${locations}"
        ERRORS=$((ERRORS + 1))
    done < <(find "${DRIVERS_DIR}" -type f -name "stdio.drv.*" -exec basename {} \; 2>/dev/null | \
        sort | uniq -d)
fi

if [[ "${ERRORS}" -gt 0 ]]; then
    echo "${ERRORS} duplicate(s) found" >&2
    exit 1
fi
echo "No duplicates found."
