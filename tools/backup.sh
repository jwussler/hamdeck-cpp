#!/bin/sh
# Bundle the whole repo and copy it to a second machine.
#
# ⚠️ WHY THIS EXISTS: "it's committed" is not a backup. Until this repo has a
# remote it lives on one disk, and a commit on a disk that dies is gone with it.
#
# ⚠️ THE BUNDLE CONTAINS THE FULL HISTORY, INCLUDING SITE DETAIL that has been
# removed from the working tree but is still in older commits. It is safe on
# private storage and MUST NOT be published. That is also why this script has no
# GitHub path in it - see the push blocker in WIP.md.
#
# No host or path is baked in: this repo is public.
#   HAMDECK_BACKUP_HOST=<ssh target> HAMDECK_BACKUP_PATH=<dir on that host> tools/backup.sh
set -e
: "${HAMDECK_BACKUP_HOST:?set HAMDECK_BACKUP_HOST to an ssh target (see SITE.md)}"
: "${HAMDECK_BACKUP_PATH:?set HAMDECK_BACKUP_PATH to a directory on that host (see SITE.md)}"

REPO=$(cd "$(dirname "$0")/.." && pwd)
STAMP=$(date +%m-%d-%Y-%H%M)
LOCAL_DIR="$HOME/backups/hamdeck-cpp"
NAME="hamdeck-cpp-$STAMP.bundle"
mkdir -p "$LOCAL_DIR"

git -C "$REPO" bundle create "$LOCAL_DIR/$NAME" --all

# Verify BEFORE sending it anywhere. A corrupt bundle copied to three machines
# is three copies of nothing.
git bundle verify "$LOCAL_DIR/$NAME" >/dev/null
echo "bundle ok: $LOCAL_DIR/$NAME"

scp -q "$LOCAL_DIR/$NAME" "$HAMDECK_BACKUP_HOST:/tmp/$NAME"
ssh "$HAMDECK_BACKUP_HOST" "mkdir -p '$HAMDECK_BACKUP_PATH' && mv /tmp/$NAME '$HAMDECK_BACKUP_PATH/'"

# ⚠️ Compare checksums. scp exiting 0 says the transfer ran, not that the bytes
# on the far end are the bytes we sent.
LOCAL_SUM=$(sha256sum "$LOCAL_DIR/$NAME" | cut -d' ' -f1)
REMOTE_SUM=$(ssh "$HAMDECK_BACKUP_HOST" "sha256sum '$HAMDECK_BACKUP_PATH/$NAME'" | cut -d' ' -f1)
if [ "$LOCAL_SUM" != "$REMOTE_SUM" ]; then
  echo "CHECKSUM MISMATCH - the remote copy is NOT intact" >&2
  exit 1
fi
echo "copied and verified: $HAMDECK_BACKUP_HOST:$HAMDECK_BACKUP_PATH/$NAME"

# Keep the last 10 locally. Old bundles are cheap; losing the only one is not.
ls -1t "$LOCAL_DIR"/*.bundle 2>/dev/null | tail -n +11 | xargs -r rm --
