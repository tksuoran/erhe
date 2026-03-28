#!/bin/bash
# scripts/git-split-commit.sh
#
# Split a git commit, extracting changes to specified files into a new
# commit that immediately follows the (now reduced) original commit.
#
# Usage:
#   scripts/git-split-commit.sh <commit> [-m "message"] -- file1 [file2 ...]
#
# Example:
#   scripts/git-split-commit.sh abc1234 -- src/foo.cpp src/foo.hpp
#   scripts/git-split-commit.sh HEAD~3 -m "Extract debug changes" -- src/debug.cpp
#
# The original commit retains its message and authorship, minus the
# extracted files. The new commit follows immediately after.
#
# To abort a failed split: git rebase --abort

set -euo pipefail

die()  { echo "Error: $*" >&2; exit 1; }
usage() {
    echo "Usage: $0 <commit> [-m <message>] -- <file1> [file2 ...]"
    exit 1
}

[ $# -ge 3 ] || usage

COMMIT="$1"; shift

EXTRACT_MSG=""
if [ "$1" = "-m" ]; then
    shift; EXTRACT_MSG="$1"; shift
fi
[ "${1:-}" = "--" ] && shift

FILES=("$@")
[ ${#FILES[@]} -gt 0 ] || die "No files specified"

# Validate
FULL_HASH=$(git rev-parse --verify "$COMMIT") || die "Bad commit: $COMMIT"
SHORT_HASH=$(git rev-parse --short "$FULL_HASH")

PARENT_COUNT=$(git cat-file -p "$FULL_HASH" | grep -c "^parent ")
[ "$PARENT_COUNT" -le 1 ] || die "Cannot split merge commits"

git diff --quiet && git diff --cached --quiet \
    || die "Working tree must be clean (stash or commit first)"

# Collect info from original commit
ORIG_MSG=$(git log -1 --format=%B "$FULL_HASH")
ORIG_AUTHOR_NAME=$(git log -1 --format="%an" "$FULL_HASH")
ORIG_AUTHOR_EMAIL=$(git log -1 --format="%ae" "$FULL_HASH")
ORIG_AUTHOR_DATE=$(git log -1 --format="%aI" "$FULL_HASH")

# All files changed in the commit
mapfile -t ALL_CHANGED < <(git diff-tree --no-commit-id --name-only -r "$FULL_HASH")

# Partition into keep vs extract
KEEP_FILES=()
EXTRACT_FILES=()
for f in "${ALL_CHANGED[@]}"; do
    matched=false
    for pat in "${FILES[@]}"; do
        if [ "$f" = "$pat" ]; then
            matched=true; break
        fi
    done
    if $matched; then
        EXTRACT_FILES+=("$f")
    else
        KEEP_FILES+=("$f")
    fi
done

[ ${#EXTRACT_FILES[@]} -gt 0 ] || die "None of the specified files were changed in $SHORT_HASH"
[ ${#KEEP_FILES[@]} -gt 0 ]    || die "All files would be extracted — use git rebase to drop/reorder instead"

if [ -z "$EXTRACT_MSG" ]; then
    FILE_LIST=$(printf ", %s" "${EXTRACT_FILES[@]}")
    EXTRACT_MSG="Split from ${SHORT_HASH}: ${FILE_LIST:2}"
fi

echo "Splitting $SHORT_HASH: $(git log -1 --format=%s "$FULL_HASH")"
echo "  Keep:    ${KEEP_FILES[*]}"
echo "  Extract: ${EXTRACT_FILES[*]}"
echo ""

# Write editor script to a temp file to avoid shell quoting issues across platforms.
# Use 7-char prefix (git's minimum abbreviation) so it matches regardless of the
# abbreviation length git chooses for the todo list.
MATCH_PREFIX="${FULL_HASH:0:7}"
EDITOR_SCRIPT=$(mktemp)
trap 'rm -f "$EDITOR_SCRIPT"' EXIT
cat > "$EDITOR_SCRIPT" << EDITEOF
#!/bin/bash
sed -i "s/^pick ${MATCH_PREFIX}/edit ${MATCH_PREFIX}/" "\$1"
EDITEOF
chmod +x "$EDITOR_SCRIPT"

GIT_SEQUENCE_EDITOR="$EDITOR_SCRIPT" \
    git rebase -i "${FULL_HASH}^" || die "Rebase failed to start"

# Verify we actually stopped at the target commit. If the sed pattern didn't match,
# the rebase completes without stopping and subsequent commands would damage history.
if [ ! -d ".git/rebase-merge" ]; then
    die "Rebase completed without stopping — hash pattern '${MATCH_PREFIX}' did not match in the todo list"
fi

# Stopped at the commit. Mixed reset to unstage everything from it.
git reset HEAD~1 --quiet

# Stage only the files to keep, commit with original message/authorship
git add -- "${KEEP_FILES[@]}"
GIT_AUTHOR_NAME="$ORIG_AUTHOR_NAME" \
GIT_AUTHOR_EMAIL="$ORIG_AUTHOR_EMAIL" \
GIT_AUTHOR_DATE="$ORIG_AUTHOR_DATE" \
git commit --quiet -m "$ORIG_MSG"

# Stage the extracted files, commit
git add -- "${EXTRACT_FILES[@]}"
git commit --quiet -m "$EXTRACT_MSG"

# Save the two new commit hashes before rebase rewrites them
KEPT_HASH=$(git rev-parse --short HEAD~1)
EXTRACTED_HASH=$(git rev-parse --short HEAD)

# Finish rebase
git rebase --continue || die "Rebase --continue failed (conflicts?)"

echo "Done. Original $SHORT_HASH split into:"
echo "  $KEPT_HASH $(git log -1 --format=%s "$KEPT_HASH" 2>/dev/null || echo '(rebased)')"
echo "  $EXTRACTED_HASH $(git log -1 --format=%s "$EXTRACTED_HASH" 2>/dev/null || echo '(rebased)')"
