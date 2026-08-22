#!/usr/bin/env bash
#
# BitFinite Core — how far has this fork drifted from the upstream it came from?
#
# Answers three questions a forked chain cannot otherwise answer about itself:
#
#   1. Which upstream release did we actually fork from?
#   2. What has upstream changed since, and how much of it can we take cleanly?
#   3. Which of those changes are FIXES we are missing, versus CONSENSUS changes
#      we deliberately did not follow?
#
# ----------------------------------------------------------------------------
# WHY THIS DOES NOT USE COMMIT ANCESTRY
#
# The obvious implementation — `git log upstream/master ^HEAD` — is worse than
# useless on this repo, and on most forked chains, because their history was
# re-rooted at fork time. Measured here on 2026-08-22:
#
#   git merge-base --is-ancestor v27.0.0 HEAD   ->  NOT an ancestor
#   git merge-base HEAD v27.0.0                 ->  a 2016 secp256k1 squash commit
#   git rev-list --count HEAD..v28.0.1          ->  21,260 "missing" commits
#
# Those 21,260 include the fix for CVE-2013-5700, which is demonstrably already
# in our src/bloom.h. The commit graph says we diverged in 2016; we actually
# forked BCHN v27.0.0 in 2023. Ancestry is measuring history rewrites, not code.
#
# So the fork point is found by TREE HASH instead. A commit whose tree is
# byte-identical to an upstream release tag's tree IS that release, whatever its
# hash or parentage says. That works on any fork, however its history was built.
#
# ----------------------------------------------------------------------------
# WHY THE OUTPUT IS TRIAGE AND NOT A PATCH LIST
#
# "491 files upstream changed that you never touched" is NOT 491 safe backports.
# On this repo the largest such file is src/script/bigint.cpp at 710 lines — BCH's
# arbitrary-precision VM work, a CHIP. Taking it would HARD FORK the chain.
#
# A drift tool that cannot separate "you are missing a fix" from "upstream changed
# consensus and you chose not to follow" is dangerous, so this one refuses to
# guess: it sorts files into consensus-sensitive and not, and leaves the call to
# a human. The consensus bucket is small enough to read.
#
# ----------------------------------------------------------------------------
# Usage:
#   scripts/fork-drift.sh                     # against the newest upstream tag
#   scripts/fork-drift.sh v28.0.0             # against a specific tag
#   scripts/fork-drift.sh --json              # machine-readable
#   UPSTREAM_REMOTE=bchn scripts/fork-drift.sh
#
# First run adds and fetches the upstream remote (~200MB). Later runs reuse it;
# pass --fetch to refresh.
set -uo pipefail
cd "$(dirname "$0")/.."

UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-bchn}"
UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/bitcoin-cash-node/bitcoin-cash-node.git}"
TAG_GLOB="${TAG_GLOB:-v2[0-9].*}"

JSON=0; WANT_FETCH=0; TARGET=""
for a in "$@"; do
  case "$a" in
    --json)  JSON=1 ;;
    --fetch) WANT_FETCH=1 ;;
    -h|--help) sed -n '2,50p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) TARGET="$a" ;;
  esac
done

say() { [ "$JSON" = 1 ] || printf '%s\n' "$*"; }

# ---- upstream available? -----------------------------------------------------
if ! git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
  say "adding remote $UPSTREAM_REMOTE -> $UPSTREAM_URL"
  git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL" || exit 1
  WANT_FETCH=1
fi
if [ "$WANT_FETCH" = 1 ] || [ -z "$(git tag -l "$TAG_GLOB" | head -1)" ]; then
  say "fetching $UPSTREAM_REMOTE tags (large, first time only)…"
  git fetch -q --tags "$UPSTREAM_REMOTE" || { echo "fetch failed" >&2; exit 1; }
fi

TAGS=$(git tag -l "$TAG_GLOB" | sort -V)
[ -n "$TAGS" ] || { echo "no upstream tags matching $TAG_GLOB" >&2; exit 1; }
NEWEST=$(printf '%s\n' "$TAGS" | tail -1)
TARGET="${TARGET:-$NEWEST}"
git rev-parse -q --verify "$TARGET^{commit}" >/dev/null || { echo "unknown tag: $TARGET" >&2; exit 1; }

# ---- 1. locate the fork point by TREE hash ----------------------------------
# Build tree -> tag once, then walk our history until a tree matches. Bounded by
# our own history length, and it stops at the first (newest) match.
declare -A TREE2TAG
while read -r t; do
  [ -n "$t" ] && TREE2TAG["$(git rev-parse "$t^{tree}")"]="$t"
done <<< "$TAGS"

FORK_COMMIT=""; FORK_TAG=""
while read -r c tr; do
  if [ -n "${TREE2TAG[$tr]:-}" ]; then FORK_COMMIT="$c"; FORK_TAG="${TREE2TAG[$tr]}"; break; fi
done < <(git log --format='%H %T' HEAD)

if [ -z "$FORK_TAG" ]; then
  echo "no upstream release tree matches any commit in this history." >&2
  echo "either the fork predates every tag, or the tree was modified at fork time." >&2
  exit 2
fi

# ---- 2. drift ----------------------------------------------------------------
git diff --name-only "$FORK_TAG" "$TARGET" -- src > /tmp/fd-up.$$ 2>/dev/null
git diff --name-only "$FORK_TAG" HEAD      -- src > /tmp/fd-ours.$$ 2>/dev/null
trap 'rm -f /tmp/fd-up.$$ /tmp/fd-ours.$$' EXIT

UP=$(wc -l < /tmp/fd-up.$$)
CLEAN=$(comm -23 <(sort /tmp/fd-up.$$) <(sort /tmp/fd-ours.$$) | wc -l)
CONFLICT=$(comm -12 <(sort /tmp/fd-up.$$) <(sort /tmp/fd-ours.$$) | wc -l)

# releases between the fork and the target, so "N behind" is a real number
BEHIND=$(printf '%s\n' "$TAGS" | awk -v a="$FORK_TAG" -v b="$TARGET" '
  $0==a{s=1;next} s&&$0!=b{n++} $0==b{print n+1;exit}')

# ---- 3. classify -------------------------------------------------------------
# Consensus-sensitive paths. A change here is either a fix we want or a CHIP we
# deliberately declined; either way a human reads it. Everything else is triage
# by volume.
CONSENSUS_RE='^src/(consensus|script|policy|validation\.|pow\.|primitives)'

area_of() {
  case "$1" in
    src/qt/*)                 echo "qt / GUI" ;;
    src/test/*|*/test/*)      echo "tests" ;;
    src/bench/*)              echo "bench" ;;
    src/seeder/*)             echo "seeder" ;;
    src/wallet/*)             echo "wallet" ;;
    src/rpc/*)                echo "rpc" ;;
    src/net*|src/addrman*)    echo "net / p2p" ;;
    *) if [[ "$1" =~ $CONSENSUS_RE ]]; then echo "CONSENSUS"; else echo "other core"; fi ;;
  esac
}

if [ "$JSON" = 1 ]; then
  printf '{"fork_tag":"%s","fork_commit":"%s","target":"%s","releases_behind":%s,' \
    "$FORK_TAG" "${FORK_COMMIT:0:12}" "$TARGET" "${BEHIND:-0}"
  printf '"upstream_changed":%s,"clean_candidates":%s,"needs_review":%s,"consensus":[' "$UP" "$CLEAN" "$CONFLICT"
  first=1
  grep -E "$CONSENSUS_RE" /tmp/fd-up.$$ | while read -r f; do
    u=$(git diff --numstat "$FORK_TAG" "$TARGET" -- "$f" | awk '{print $1+$2}')
    o=$(git diff --numstat "$FORK_TAG" HEAD -- "$f" | awk '{print $1+$2}')
    [ "$first" = 1 ] && first=0 || printf ','
    printf '{"file":"%s","upstream_lines":%s,"our_lines":%s}' "$f" "${u:-0}" "${o:-0}"
  done
  printf ']}\n'
  exit 0
fi

echo
echo "  FORK DRIFT — $(basename "$(pwd)")"
echo "  ────────────────────────────────────────────────────────────────"
printf '  forked from   %s  (tree-identical at our %s, %s)\n' \
  "$FORK_TAG" "${FORK_COMMIT:0:10}" "$(git log -1 --format=%ad --date=short "$FORK_COMMIT")"
printf '  compared to   %s  (%s)\n' "$TARGET" "$(git log -1 --format=%ad --date=short "$TARGET")"
printf '  releases behind: %s\n' "${BEHIND:-?}"
echo
printf '  upstream changed %s src files since the fork\n' "$UP"
printf '    %-6s we never touched   → clean backport candidates\n' "$CLEAN"
printf '    %-6s we also modified   → manual review\n' "$CONFLICT"
echo
echo "  by area:"
while read -r f; do area_of "$f"; done < /tmp/fd-up.$$ | sort | uniq -c | sort -rn \
  | awk '{ $1=$1; n=$1; $1=""; printf "    %-22s %s\n", substr($0,2), n }'
echo
echo "  CONSENSUS-SENSITIVE — read these, do not bulk-apply:"
echo "    (a large upstream delta we have not touched is as likely to be a CHIP"
echo "     we declined as a fix we are missing — bigint.cpp is the worked example)"
grep -E "$CONSENSUS_RE" /tmp/fd-up.$$ | while read -r f; do
  u=$(git diff --numstat "$FORK_TAG" "$TARGET" -- "$f" | awk '{print $1+$2}')
  o=$(git diff --numstat "$FORK_TAG" HEAD -- "$f" | awk '{print $1+$2}')
  printf '    %-44s upstream %-6s ours %s\n' "$f" "${u:-0}" "${o:-0}"
done | sort -k3 -rn
echo
