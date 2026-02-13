#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  tools/merge_milestones.sh [options] <start> <end>

Merge milestone markdown files from doc/milestone/ into a single file.

Arguments:
  <start>   Start milestone number (inclusive)
  <end>     End milestone number (inclusive)

Options:
  -o, --output <file>   Output file path (default: stdout)
  -s, --separator       Insert horizontal rule between milestones (default: yes)
  --no-separator        No separator between milestones
  --toc                 Prepend a table of contents
  -h, --help            Show this help

Examples:
  tools/merge_milestones.sh 49 57
  tools/merge_milestones.sh 49 57 -o doc/milestones_49_57.md
  tools/merge_milestones.sh 49 57 --toc -o combined.md
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MILESTONE_DIR="${ROOT_DIR}/doc/milestone"

OUTPUT=""
SEPARATOR=1
TOC=0

# --- parse args ---
POSITIONAL=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o|--output)   OUTPUT="$2"; shift 2 ;;
        -s|--separator) SEPARATOR=1; shift ;;
        --no-separator) SEPARATOR=0; shift ;;
        --toc)         TOC=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        -*)            echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
        *)             POSITIONAL+=("$1"); shift ;;
    esac
done

if [[ ${#POSITIONAL[@]} -ne 2 ]]; then
    echo "Error: expected exactly 2 positional arguments (start end)" >&2
    usage >&2
    exit 1
fi

START="${POSITIONAL[0]}"
END="${POSITIONAL[1]}"

if ! [[ "$START" =~ ^[0-9]+$ ]] || ! [[ "$END" =~ ^[0-9]+$ ]]; then
    echo "Error: start and end must be integers" >&2
    exit 1
fi

if [[ "$START" -gt "$END" ]]; then
    echo "Error: start ($START) must be <= end ($END)" >&2
    exit 1
fi

# --- collect files ---
FILES=()
TITLES=()
for i in $(seq "$START" "$END"); do
    # milestone files use zero-padded 2-digit numbers for 01-09
    pattern="${MILESTONE_DIR}/milestone_${i}_*.md"
    # also try zero-padded
    pattern_padded="${MILESTONE_DIR}/milestone_$(printf '%02d' "$i")_*.md"

    found=""
    for f in $pattern $pattern_padded; do
        if [[ -f "$f" ]]; then
            found="$f"
            break
        fi
    done

    if [[ -z "$found" ]]; then
        echo "Warning: milestone $i not found, skipping" >&2
        continue
    fi

    FILES+=("$found")
    # extract first H1 line as title
    title=$(grep -m1 '^# ' "$found" | sed 's/^# //')
    TITLES+=("$title")
done

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "Error: no milestone files found in range $START..$END" >&2
    exit 1
fi

# --- generate output ---
{
    # optional TOC
    if [[ "$TOC" -eq 1 ]]; then
        echo "# 目录"
        echo ""
        for idx in "${!TITLES[@]}"; do
            # generate anchor: lowercase ASCII, keep CJK/unicode, spaces to hyphens, strip punctuation
            anchor=$(echo "${TITLES[$idx]}" | tr '[:upper:]' '[:lower:]' | sed 's/ /-/g' | perl -CSD -pe 's/[^\p{L}\p{N}_-]//g')
            echo "- [${TITLES[$idx]}](#${anchor})"
        done
        echo ""
        echo "---"
        echo ""
    fi

    first=1
    for idx in "${!FILES[@]}"; do
        if [[ "$first" -eq 0 && "$SEPARATOR" -eq 1 ]]; then
            echo ""
            echo "---"
            echo ""
        fi
        cat "${FILES[$idx]}"
        # ensure trailing newline
        echo ""
        first=0
    done
} | if [[ -n "$OUTPUT" ]]; then
    mkdir -p "$(dirname "$OUTPUT")"
    cat > "$OUTPUT"
    echo "Merged ${#FILES[@]} milestones ($START..$END) -> $OUTPUT" >&2
else
    cat
fi
