#!/bin/bash
# tools/verify-webui-build.sh
# Verify WebUI build artifacts

set -e

cd src/webui

echo "=== WebUI Build Verification ==="

# 1. Build
npm run build

# 2. Check artifacts exist
if [ ! -f dist/index.html ]; then
  echo "ERROR: dist/index.html not found"
  exit 1
fi

# 3. Check build size
TOTAL_SIZE=$(du -sk dist/ | cut -f1)
echo "Total build size: ${TOTAL_SIZE}KB"

if [ "$TOTAL_SIZE" -gt 10240 ]; then
  echo "WARNING: Build size exceeds 10MB"
fi

# 4. Check key files
JS_COUNT=$(find dist/assets -name "*.js" | wc -l)
CSS_COUNT=$(find dist/assets -name "*.css" | wc -l)
echo "JS chunks: $JS_COUNT"
echo "CSS files: $CSS_COUNT"

echo "=== Build verification passed ==="
