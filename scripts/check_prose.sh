#!/usr/bin/env bash
# Fails if any prose file contains an em dash, a contraction, or a placeholder
# marker. Prose files: Markdown, WISA kernels, Python, C++ comments are covered
# by scanning whole files; code identifiers never contain these tokens.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
status=0
files=$(git ls-files '*.md' '*.wisa' '*.py' '*.cpp' '*.hpp' '*.txt' '*.yml' '*.json' 'Makefile' 'Dockerfile' )
if echo "$files" | xargs grep -nE "—" ; then echo "check_prose: em dash found"; status=1; fi
pattern="\b(don't|doesn't|isn't|aren't|can't|cannot't|won't|wouldn't|shouldn't|couldn't|it's|that's|there's|we're|you're|they're|I'm|I've|we've|let's|didn't|wasn't|weren't|hasn't|haven't|hadn't|what's|who's)\b"
if echo "$files" | xargs grep -nEi "$pattern" ; then echo "check_prose: contraction found"; status=1; fi
if echo "$files" | xargs grep -nE "\b(TODO|FIXME|XXX|TBD|PLACEHOLDER|lorem ipsum)\b" ; then echo "check_prose: placeholder found"; status=1; fi
if [ $status -eq 0 ]; then echo "check_prose: ok"; fi
exit $status
