#!/usr/bin/env bash
# Protect main. ⚠️ ONLY WORKS ON A PUBLIC REPO (or GitHub Pro) - the API answers
# 403 "Upgrade to GitHub Pro or make this repository public" otherwise, which is
# why this is a script to run at the moment of flipping rather than a setting
# somebody is supposed to remember afterwards.
#
# What it does and does NOT do, deliberately:
#   * requires the three CI checks to pass          - the gates are the point
#   * blocks force-push and deletion of main        - the two irreversible ones
#   * requires a pull request, with ZERO approvals  - a solo maintainer cannot
#     approve their own PR, so requiring one would lock the repo against its
#     only committer. The PR requirement still buys the CI gate and a diff to
#     read before merging.
#   * lets the repo ADMIN bypass                    - so a broken CI config can
#     never leave you unable to fix your own repository.
set -euo pipefail
REPO="${1:-jwussler/hamdeck-cpp}"
cd "$(dirname "$0")"
echo "applying the main ruleset to $REPO"
gh api --method POST "repos/$REPO/rulesets" --input main-ruleset.json \
  --jq '"created ruleset \(.id): \(.name) (\(.enforcement))"'
echo "verifying it is actually there:"
gh api "repos/$REPO/rulesets" --jq '.[] | "  \(.name)  \(.enforcement)"'
