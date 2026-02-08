#!/bin/bash
python ./tools/run-clang-tidy.py -p build -j 8 -quiet -config-file .clang-tidy "stdiolink/src" 2>&1 \
    | grep -E "^/.*warning:" \
    | grep -v "vcpkg_installed" \
    | sort -u
