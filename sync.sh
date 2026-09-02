#!/bin/sh
# Push the working tree to the build VM (105, ssh alias `deck`) and build there.
# The VM is the build host on purpose: it is where the ALSA and serial work will
# run, so it is the only place a green build means anything.
#
# No default host is baked in. docs/internal/CARRYOVER.md section 6: never ship a default host -
# a hostname in a public repo points every install at that station. Set
# HAMDECK_BUILD_HOST to an ssh target; the site's value is in the gitignored SITE.md.
set -e
: "${HAMDECK_BUILD_HOST:?set HAMDECK_BUILD_HOST to an ssh target (see SITE.md)}"
rsync -a --delete \
  --exclude '.git' --exclude 'build' --exclude 'SITE.md' \
  "$(dirname "$0")/" "$HAMDECK_BUILD_HOST":~/hamdeck-cpp/
ssh "$HAMDECK_BUILD_HOST" 'cmake -S ~/hamdeck-cpp -B ~/hamdeck-cpp/build -G Ninja >/dev/null && \
          cmake --build ~/hamdeck-cpp/build'
