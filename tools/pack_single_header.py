#!/usr/bin/env python3
"""
将指定目录的 C++ 头文件和源文件打包成单头文件。

自行实现 #include 展开，不依赖 quom（其 tokenizer 对部分 C++ 字面量有 bug）。

用法:
    pack_single_header.py <dir_path> [--output <file>] [--include-dir <dir>]

示例:
    pack_single_header.py src/stdiolink
    pack_single_header.py src/stdiolink --output out/my_lib.hpp
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent
SRC_DIR = PROJECT_ROOT / "src"
OUTPUT_DIR = PROJECT_ROOT / "single_include"

HEADER_EXTENSIONS = {".h", ".hpp", ".hxx"}
SOURCE_EXTENSIONS = {".c", ".cpp", ".cxx", ".cc"}

# 匹配 #include "xxx" （不匹配 <xxx>）
_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
# 匹配 #pragma once
_PRAGMA_ONCE_RE = re.compile(r'^\s*#\s*pragma\s+once\b')
# 匹配 include guard 模式: #ifndef XXX / #define XXX ... #endif
_IFNDEF_RE = re.compile(r'^\s*#\s*ifndef\s+(\w+)')
_DEFINE_RE = re.compile(r'^\s*#\s*define\s+(\w+)')
_ENDIF_RE = re.compile(r'^\s*#\s*endif\b')


class SingleHeaderPacker:
    """递归展开 #include，收集头文件和对应源文件，输出单头文件。"""

    def __init__(self, include_dirs: list[Path]):
        self._include_dirs = include_dirs
        self._included: set[Path] = set()  # 已处理的文件（去重）
        self._header_lines: list[str] = []  # 展开后的头文件内容
        self._source_files: list[Path] = []  # 待拼接的源文件

    def process_entry(self, entry_file: Path):
        """从入口头文件开始递归展开。"""
        self._expand_file(entry_file)

    def write(self, output: Path, guard_name: str):
        """将结果写入输出文件。"""
        lines: list[str] = []
        lines.append(f"#ifndef {guard_name}")
        lines.append(f"#define {guard_name}")
        lines.append("")

        # 头文件部分
        lines.extend(self._header_lines)

        # 源文件部分
        if self._source_files:
            impl_macro = guard_name.replace("_SINGLE_HEADER_HPP", "_IMPLEMENTATION")
            lines.append("")
            lines.append("// ============ Implementation ============")
            lines.append(f"#ifdef {impl_macro}")
            lines.append("")
            for src in self._source_files:
                lines.extend(self._read_source(src))
                lines.append("")
            lines.append(f"#endif // {impl_macro}")

        lines.append("")
        lines.append(f"#endif // {guard_name}")
        lines.append("")

        content = "\n".join(lines)
        # 压缩连续空行为最多两个
        content = re.sub(r'\n{4,}', '\n\n\n', content)
        output.write_text(content, encoding="utf-8")

    def _resolve_include(self, include_path: str, from_file: Path) -> Path | None:
        """解析 #include "xxx" 的实际文件路径。"""
        # 先相对于当前文件目录查找
        candidate = (from_file.parent / include_path).resolve()
        if candidate.is_file():
            return candidate
        # 再在 include 目录中查找
        for d in self._include_dirs:
            candidate = (d / include_path).resolve()
            if candidate.is_file():
                return candidate
        return None

    def _find_source_file(self, header_path: Path) -> Path | None:
        """查找头文件对应的源文件（同目录、同名）。"""
        for ext in SOURCE_EXTENSIONS:
            src = header_path.with_suffix(ext)
            if src.is_file():
                return src
        return None

    def _strip_include_guard(self, lines: list[str]) -> list[str]:
        """去除 #ifndef/#define/#endif 形式的 include guard。"""
        if len(lines) < 3:
            return lines

        # 找第一个非空行
        first_idx = None
        for i, line in enumerate(lines):
            if line.strip():
                first_idx = i
                break
        if first_idx is None:
            return lines

        m_ifndef = _IFNDEF_RE.match(lines[first_idx])
        if not m_ifndef:
            return lines

        guard_sym = m_ifndef.group(1)

        # 下一个非空行应该是 #define SAME_SYMBOL
        define_idx = None
        for i in range(first_idx + 1, len(lines)):
            if lines[i].strip():
                define_idx = i
                break
        if define_idx is None:
            return lines

        m_define = _DEFINE_RE.match(lines[define_idx])
        if not m_define or m_define.group(1) != guard_sym:
            return lines

        # 最后一个非空行应该是 #endif
        last_idx = None
        for i in range(len(lines) - 1, define_idx, -1):
            if lines[i].strip():
                last_idx = i
                break
        if last_idx is None:
            return lines

        if not _ENDIF_RE.match(lines[last_idx]):
            return lines

        # 去掉这三行
        result = lines[:first_idx] + lines[define_idx + 1:last_idx] + lines[last_idx + 1:]
        return result

    def _expand_file(self, file_path: Path):
        """递归展开一个头文件。"""
        resolved = file_path.resolve()
        if resolved in self._included:
            return
        self._included.add(resolved)

        text = resolved.read_text(encoding="utf-8")
        lines = text.splitlines()

        # 去除 #pragma once
        lines = [l for l in lines if not _PRAGMA_ONCE_RE.match(l)]
        # 去除 include guard
        lines = self._strip_include_guard(lines)

        for line in lines:
            m = _INCLUDE_RE.match(line)
            if m:
                inc_path = m.group(1)
                target = self._resolve_include(inc_path, resolved)
                if target and target.resolve() not in self._included:
                    self._expand_file(target)
                # 内部 include 不输出到结果中（已展开）
                # 但如果找不到文件，保留原样（可能是系统头文件用了双引号）
                if not target:
                    self._header_lines.append(line)
            else:
                self._header_lines.append(line)

        # 收集对应的源文件
        src = self._find_source_file(resolved)
        if src and src.resolve() not in self._included:
            self._included.add(src.resolve())
            self._source_files.append(src)

    def _read_source(self, src_path: Path) -> list[str]:
        """读取源文件内容，去掉对已展开头文件的 #include。"""
        text = src_path.read_text(encoding="utf-8")
        result: list[str] = []
        result.append(f"// --- {src_path.name} ---")
        for line in text.splitlines():
            m = _INCLUDE_RE.match(line)
            if m:
                inc_path = m.group(1)
                target = self._resolve_include(inc_path, src_path)
                if target and target.resolve() in self._included:
                    continue  # 已展开，跳过
                # 找不到的保留（可能是系统头文件）
            result.append(line)
        return result


def collect_headers(target_dir: Path, include_base: Path) -> list[str]:
    """递归收集目录下所有头文件，返回相对于 include_base 的路径列表。"""
    headers = []
    for f in sorted(target_dir.rglob("*")):
        if f.is_file() and f.suffix in HEADER_EXTENSIONS:
            rel = f.relative_to(include_base)
            headers.append(rel.as_posix())
    return headers


def create_entry_header(target_dir: Path, include_base: Path) -> Path:
    """根据目录内容自动生成入口头文件。"""
    headers = collect_headers(target_dir, include_base)
    if not headers:
        print(f"Warning: no header files found in {target_dir}")

    dir_name = target_dir.name
    lines = [
        f"// {dir_name} single header",
        "// Generated by pack_single_header.py",
        "",
        "#pragma once",
        "",
    ]

    # 按子目录分组输出
    current_group = None
    for h in headers:
        parts = Path(h).parts
        group = parts[1] if len(parts) > 2 else None
        if group != current_group:
            if current_group is not None:
                lines.append("")
            if group:
                lines.append(f"// {group}")
            current_group = group
        lines.append(f'#include "{h}"')

    lines.append("")
    content = "\n".join(lines)

    entry_path = include_base / f"{dir_name}.h"
    entry_path.write_text(content, encoding="utf-8")
    return entry_path


def pack_single_header(
    target_dir: Path, output_file: Path | None, include_dir: Path | None
):
    """打包单头文件。"""
    target_dir = target_dir.resolve()
    if not target_dir.is_dir():
        print(f"Error: {target_dir} is not a directory")
        return False

    # include 搜索根目录：默认为 target_dir 的父目录
    include_base = (include_dir or target_dir.parent).resolve()

    # 输出文件：默认 single_include/<dir_name>.hpp
    if output_file is None:
        OUTPUT_DIR.mkdir(exist_ok=True)
        output_file = OUTPUT_DIR / f"{target_dir.name}.hpp"

    output_file.parent.mkdir(parents=True, exist_ok=True)

    # 生成入口头文件
    entry_header = create_entry_header(target_dir, include_base)
    print(f"Created entry header: {entry_header}")

    # include guard 名称
    guard_name = target_dir.name.upper().replace("-", "_") + "_SINGLE_HEADER_HPP"

    # 展开并打包
    packer = SingleHeaderPacker(include_dirs=[include_base])
    packer.process_entry(entry_header)
    packer.write(output_file, guard_name)

    # 删除临时入口头文件
    entry_header.unlink(missing_ok=True)
    print(f"Removed temp entry header: {entry_header}")

    print(f"Generated: {output_file}")
    print(f"File size: {output_file.stat().st_size} bytes")
    print(f"Headers expanded: {len(packer._included) - len(packer._source_files)}")
    print(f"Source files stitched: {len(packer._source_files)}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="将指定目录的 C++ 头文件和源文件打包成单头文件"
    )
    parser.add_argument(
        "dir_path",
        nargs="?",
        default=".",
        help="要打包的目录路径 (默认: 当前目录)",
    )
    parser.add_argument(
        "--output",
        "-o",
        default=None,
        help="输出文件路径 (默认: single_include/<目录名>.hpp)",
    )
    parser.add_argument(
        "--include-dir",
        "-I",
        default=None,
        help="include 搜索根目录 (默认: 目标目录的父目录)",
    )
    args = parser.parse_args()

    target_dir = Path(args.dir_path)
    output_file = Path(args.output) if args.output else None
    include_dir = Path(args.include_dir) if args.include_dir else None

    print("=" * 50)
    print(f"Packing {target_dir.name} to single header file")
    print("=" * 50)

    if pack_single_header(target_dir, output_file, include_dir):
        print("\nSuccess!")
        return 0
    else:
        print("\nFailed!")
        return 1


if __name__ == "__main__":
    sys.exit(main())