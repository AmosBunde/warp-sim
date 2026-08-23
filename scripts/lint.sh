#!/usr/bin/env bash
# Runs clang-format in check mode and clang-tidy over every first-party source.
# Exit status is non-zero on any formatting diff or any clang-tidy diagnostic.
# Usage: scripts/lint.sh [build-dir]   (build-dir must hold compile_commands.json)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$root/build/debug}"

clang_format="${CLANG_FORMAT:-clang-format}"
clang_tidy="${CLANG_TIDY:-clang-tidy}"

mapfile -t sources < <(find "$root/include" "$root/src" "$root/tests" "$root/python" \
    \( -name '*.cpp' -o -name '*.hpp' \) 2>/dev/null | sort)

if [[ ${#sources[@]} -eq 0 ]]; then
    echo "lint: no sources found" >&2
    exit 1
fi

echo "lint: clang-format check on ${#sources[@]} files"
"$clang_format" --dry-run --Werror "${sources[@]}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "lint: missing $build_dir/compile_commands.json, configure the debug preset first" >&2
    exit 1
fi

mapfile -t translation_units < <(printf '%s\n' "${sources[@]}" | grep '\.cpp$')
echo "lint: clang-tidy on ${#translation_units[@]} translation units"
"$clang_tidy" -p "$build_dir" --quiet "${translation_units[@]}"
echo "lint: ok"
