#!/usr/bin/env python3
"""
使用 quom 将 stdiolink 库打包成单头文件
"""

import subprocess
import sys
import os
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent
SRC_DIR = PROJECT_ROOT / "src"
OUTPUT_DIR = PROJECT_ROOT / "single_include"


def create_entry_header():
    """创建入口头文件，包含所有公开的头文件"""
    entry_content = """// stdiolink 单头文件入口
// 此文件由 pack_single_header.py 自动生成

#pragma once

// Protocol
#include "stdiolink/protocol/jsonl_types.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include "stdiolink/protocol/jsonl_parser.h"

// Driver
#include "stdiolink/driver/iresponder.h"
#include "stdiolink/driver/icommand_handler.h"
#include "stdiolink/driver/stdio_responder.h"
#include "stdiolink/driver/driver_core.h"

// Host
#include "stdiolink/host/task_state.h"
#include "stdiolink/host/task.h"
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"

// Console
#include "stdiolink/console/console_args.h"
#include "stdiolink/console/console_responder.h"
"""
    entry_path = SRC_DIR / "stdiolink.h"
    entry_path.write_text(entry_content, encoding="utf-8")
    return entry_path


def pack_single_header():
    """使用 quom 打包单头文件"""
    # 创建输出目录
    OUTPUT_DIR.mkdir(exist_ok=True)

    # 创建入口头文件
    entry_header = create_entry_header()
    print(f"Created entry header: {entry_header}")

    # 输出文件
    output_file = OUTPUT_DIR / "stdiolink.hpp"

    # 调用 quom
    cmd = [
        sys.executable, "-m", "quom",
        str(entry_header),
        str(output_file),
        "-I", str(SRC_DIR),
        "--include_guard", "STDIOLINK_SINGLE_HEADER_HPP",
    ]

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        return False

    print(f"Generated: {output_file}")
    print(f"File size: {output_file.stat().st_size} bytes")
    return True


def main():
    print("=" * 50)
    print("Packing stdiolink to single header file")
    print("=" * 50)

    if pack_single_header():
        print("\nSuccess!")
        return 0
    else:
        print("\nFailed!")
        return 1


if __name__ == "__main__":
    sys.exit(main())
