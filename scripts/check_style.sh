#!/bin/bash
# Reject trailing whitespace and tab-indentation drift in changed files.
# Tracked files: only added lines are checked (git diff --check + added-line scan).
# Untracked files: the whole file is new, so all its lines are checked.
# Usage: scripts/check_style.sh [git-ref]   (default: HEAD)
set -u
cd "$(dirname "$0")/.."

REF="${1:-HEAD}"
rc=0

# Tracked changes: git --check gives exact file:line for trailing whitespace.
git diff --check "$REF" -- || rc=1

# Added lines: reject a tab in the leading indentation of space-indented files.
git diff --no-color -U0 "$REF" -- | awk '
    /^\+\+\+/ {
        if ($0 ~ /^\+\+\+ b\//) file = substr($0, 7); else file = ""
        next
    }
    /^\+/ {
        if (file == "") next
        if (!(file ~ /\.(cpp|hpp|h|sh|cyan|md|txt|py)$/ || file == "CMakeLists.txt")) next
        n[file]++
        line = substr($0, 2)
        tmp = line; sub(/^ */, "", tmp)
        if (substr(tmp, 1, 1) == "\t") {
            printf "%s: added line %d: tab indentation\n", file, n[file]
            bad = 1
        }
    }
    END { exit bad }
' || rc=1

# Untracked files: check every line.
while IFS= read -r f; do
    [ -f "$f" ] || continue
    case "$f" in
        *.cpp|*.hpp|*.h|*.sh|*.cyan|*.md|*.txt|*.py|CMakeLists.txt) ;;
        *) continue ;;
    esac
    awk -v f="$f" '
        /[ \t]+$/ { printf "%s:%d: trailing whitespace\n", f, NR; bad = 1 }
        /^[ ]*\t/ { printf "%s:%d: tab indentation\n", f, NR; bad = 1 }
        END { exit bad }
    ' "$f" || rc=1
done < <(git ls-files --others --exclude-standard)

exit "$rc"
