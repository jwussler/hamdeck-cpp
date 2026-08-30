#!/bin/sh
# Push the working tree to the build VM (105, ssh alias `deck`) and build there.
# The VM is the build host on purpose: it is where the ALSA and serial work will
# run, so it is the only place a green build means anything.
set -e
rsync -a --delete \
  --exclude '.git' --exclude 'build' \
  "$(dirname "$0")/" deck:~/hamdeck-cpp/
ssh <build-host> 'cmake -S ~/hamdeck-cpp -B ~/hamdeck-cpp/build -G Ninja >/dev/null && \
          cmake --build ~/hamdeck-cpp/build'
