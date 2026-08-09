#!/usr/bin/env bash
#
# BitFinite Core — in-place node upgrade from a published release.
#
# Swaps the daemon binaries and restarts, keeping the previous ones on disk for
# an instant revert. Nothing in the datadir is migrated; this is not a tool for
# upgrades that need a reindex.
#
# Usage:
#   scripts/upgrade-node.sh v3.0.2                 # upgrade
#   scripts/upgrade-node.sh v3.0.2 --dry-run       # verify artefacts, change nothing
#   scripts/upgrade-node.sh --rollback             # restore the previous binaries
#
# Configuration — environment, or an optional config file (see BFX_CONF below).
# Every value has a default derived from the RUNNING node, so on a conventional
# host you usually need to set only BFX_DEPS.
#
#   BFX_DEPS     systemd units that must stop before, and start after, the node.
#                Space-separated, stopped left-to-right and started in reverse.
#                Example: BFX_DEPS="ckpool electrs"
#   BFX_LOCKS    flock files to hold for the whole run, so cron jobs that talk
#                to the node cannot fire mid-swap. Example: /srv/payout/.lock
#   BFX_BINDIR   where the binaries live      (default: dir of the running daemon)
#   BFX_DATADIR  node data directory          (default: read from the running daemon)
#   BFX_SERVICE  systemd unit name            (default: bitfinited)
#   BFX_REPO     GitHub repo for the release  (default: bitfinitechain/bitfinite-core)
#   BFX_SHA256   expected tarball sha256      (default: looked up in the file below)
#   BFX_CONF     config file to source        (default: /etc/bitfinite/upgrade.conf)
#
# Checksums are pinned in scripts/release-checksums.txt so provenance is
# reviewed in git rather than trusted at download time. Supply BFX_SHA256 for a
# version not yet listed there.
#
# Run as the user that owns the node. sudo is used for systemctl and for
# installing into a root-owned bindir, so run it from a terminal where sudo can
# prompt — it will hang over a non-interactive SSH command.

set -euo pipefail
trap 'rc=$?; [ $rc -ne 0 ] && echo "FAILED at line $LINENO (exit $rc)" >&2; exit $rc' ERR

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECKSUMS="${BFX_CHECKSUMS:-$SELF_DIR/release-checksums.txt}"

DRY=0; ROLLBACK=0; VERSION=""
for a in "$@"; do
    case "$a" in
        --dry-run)  DRY=1 ;;
        --rollback) ROLLBACK=1 ;;
        v*)         VERSION="$a" ;;
        *) echo "unknown argument: $a" >&2; exit 2 ;;
    esac
done

# Config file is optional and never required to exist.
BFX_CONF="${BFX_CONF:-/etc/bitfinite/upgrade.conf}"
# shellcheck disable=SC1090
[ -r "$BFX_CONF" ] && . "$BFX_CONF"

SERVICE="${BFX_SERVICE:-bitfinited}"
REPO="${BFX_REPO:-bitfinitechain/bitfinite-core}"
read -r -a DEPS <<<"${BFX_DEPS:-}"
read -r -a LOCKS <<<"${BFX_LOCKS:-}"
BINS=(bitfinited bitfinite-cli bitfinite-tx)   # GUI/wallet tools are not installed on servers

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
run() { if [ "$DRY" = 1 ]; then echo "  [dry-run] $*"; else eval "$@"; fi; }

# ── Derive layout from the running node ─────────────────────────────────────
# Reading the live process beats hardcoding paths: it cannot disagree with
# reality, and it makes the script portable across differently-laid-out hosts.
PID=$(pgrep -x bitfinited | head -1 || true)
[ -n "$PID" ] || { echo "bitfinited is not running — start it, or investigate, before upgrading" >&2; exit 1; }
RUNNING_EXE=$(readlink -f "/proc/$PID/exe")
BINDIR="${BFX_BINDIR:-$(dirname "$RUNNING_EXE")}"

if [ -n "${BFX_DATADIR:-}" ]; then
    DATADIR="$BFX_DATADIR"
else
    DATADIR=$(tr '\0' '\n' < "/proc/$PID/cmdline" | sed -n 's/^-datadir=//p' | head -1 || true)
    DATADIR="${DATADIR:-$HOME/.bitfinite}"
fi
CLI="$BINDIR/bitfinite-cli -datadir=$DATADIR"

# ── Rollback ────────────────────────────────────────────────────────────────
if [ "$ROLLBACK" = 1 ]; then
    say "Rolling back $(hostname) to the previous binaries"
    # Only the daemon MUST have a predecessor — without it there is nothing to
    # go back to. A binary this script INTRODUCED has no backup by design and
    # is removed rather than restored; requiring a backup for every name made
    # rollback refuse to run at all.
    [ -f "$BINDIR/bitfinited.bak-pre-upgrade" ] || {
        echo "no backup at $BINDIR/bitfinited.bak-pre-upgrade — cannot roll back" >&2; exit 1; }
    for u in "${DEPS[@]}"; do run "sudo systemctl stop $u"; done
    run "sudo systemctl stop $SERVICE"
    for b in "${BINS[@]}"; do
        if [ -f "$BINDIR/$b.bak-pre-upgrade" ]; then
            run "sudo install -m755 '$BINDIR/$b.bak-pre-upgrade' '$BINDIR/$b'"; echo "  restored $b"
        else
            run "sudo rm -f '$BINDIR/$b'"; echo "  removed  $b (not present before the upgrade)"
        fi
    done
    run "sudo systemctl start $SERVICE"
    run "sleep 5"
    run "$CLI getnetworkinfo | grep subversion"
    for ((i=${#DEPS[@]}-1; i>=0; i--)); do run "sudo systemctl start ${DEPS[i]}"; done
    say "Rolled back."
    exit 0
fi

[ -n "$VERSION" ] || { echo "usage: $(basename "$0") vX.Y.Z [--dry-run] | --rollback" >&2; exit 2; }
PKG="bitfinite-${VERSION}-x86_64-linux"
URL="https://github.com/${REPO}/releases/download/${VERSION}/${PKG}.tar.gz"
WORK="${TMPDIR:-/tmp}/bfx-upgrade-${VERSION}"

# ── Preflight ───────────────────────────────────────────────────────────────
say "Preflight on $(hostname)"
HEIGHT_BEFORE=$($CLI getblockcount)
SUBVER_BEFORE=$($CLI getnetworkinfo | grep -o '/BitFinite:[^"]*' || true)
IBD=$($CLI getblockchaininfo | grep -o '"initialblockdownload": *[a-z]*' | awk '{print $2}')
FREE_MB=$(df -Pm "$BINDIR" | awk 'NR==2{print $4}')

echo "  target      : $VERSION"
echo "  running     : $RUNNING_EXE"
echo "  subversion  : $SUBVER_BEFORE"
echo "  height      : $HEIGHT_BEFORE   (ibd=$IBD)"
echo "  bindir      : $BINDIR"
echo "  datadir     : $DATADIR"
echo "  free space  : ${FREE_MB} MB"
echo "  dependents  : ${DEPS[*]:-none declared}"

# systemd only knows units that declare Requires=/Wants=; ones wired with a
# bare After= are invisible to it. Print what it does know so a missing
# BFX_DEPS entry is obvious rather than silent.
KNOWN=$(systemctl list-dependencies --reverse --plain --no-pager "$SERVICE.service" 2>/dev/null \
        | tail -n +2 | tr -d ' ' | grep -v "^$SERVICE.service$" | grep -v '\.target$' | tr '\n' ' ' || true)
[ -n "$KNOWN" ] && echo "  systemd also sees: $KNOWN"

[ "$IBD" = false ] || { echo "node is in initial block download — not a good moment" >&2; exit 1; }
[ "$FREE_MB" -ge 500 ] || { echo "need >=500 MB free, have ${FREE_MB} MB" >&2; exit 1; }

for L in "${LOCKS[@]}"; do
    [ -n "$L" ] || continue
    exec {lfd}>"$L"
    flock -n "$lfd" || { echo "could not take $L — a job is running, retry shortly" >&2; exit 1; }
    echo "  holding lock: $L"
done

# ── Fetch and verify ────────────────────────────────────────────────────────
say "Fetching and verifying $PKG"
SHA="${BFX_SHA256:-}"
if [ -z "$SHA" ] && [ -r "$CHECKSUMS" ]; then
    SHA=$(awk -v f="$PKG.tar.gz" '$2==f {print $1}' "$CHECKSUMS" | head -1 || true)
fi
[ -n "$SHA" ] || { echo "no pinned sha256 for $PKG.tar.gz — add it to $CHECKSUMS or set BFX_SHA256" >&2; exit 1; }

mkdir -p "$WORK"; cd "$WORK"
[ -f "$PKG.tar.gz" ] || curl -fsSL -o "$PKG.tar.gz" "$URL"
echo "$SHA  $PKG.tar.gz" | sha256sum -c -
rm -rf "$PKG"; tar xzf "$PKG.tar.gz"

# The package ships its own per-binary SHA256SUMS; check the files we install
# against it so a correct tarball with a swapped member cannot slip through.
( cd "$PKG" && for b in "${BINS[@]}"; do grep -E "  ($b|bin/$b)$" SHA256SUMS | sed "s#bin/##" > /tmp/.bfxsum.$$ || true
    [ -s /tmp/.bfxsum.$$ ] && ( cd bin && sha256sum -c /tmp/.bfxsum.$$ ); rm -f /tmp/.bfxsum.$$; done )

NEWVER=$("./$PKG/bin/bitfinited" -version | head -1)
echo "  new binary reports: $NEWVER"
grep -q "${VERSION#v}" <<<"$NEWVER" || { echo "binary does not report $VERSION" >&2; exit 1; }

if [ "$DRY" = 1 ]; then
    say "Dry run complete — verified artefacts, changed nothing."
    exit 0
fi

# ── Stop, back up, swap ─────────────────────────────────────────────────────
say "Stopping dependents, then the node"
for u in "${DEPS[@]}"; do sudo systemctl stop "$u"; echo "  stopped $u"; done
sudo systemctl stop "$SERVICE"
for _ in $(seq 1 60); do pgrep -x bitfinited >/dev/null || break; sleep 1; done
pgrep -x bitfinited >/dev/null && { echo "bitfinited did not exit within 60s" >&2; exit 1; }
echo "  node stopped cleanly"

say "Backing up datadir and current binaries"
tar czf "$HOME/bfx-datadir-pre-${VERSION}-$(date +%Y%m%d-%H%M%S).tar.gz" \
    -C "$(dirname "$DATADIR")" --exclude="$(basename "$DATADIR")/debug.log" "$(basename "$DATADIR")"
for b in "${BINS[@]}"; do
    [ -f "$BINDIR/$b" ] && sudo cp -a "$BINDIR/$b" "$BINDIR/$b.bak-pre-upgrade"
done
echo "  datadir archived; binaries kept as *.bak-pre-upgrade"

say "Installing $VERSION"
for b in "${BINS[@]}"; do
    sudo install -m755 "$WORK/$PKG/bin/$b" "$BINDIR/$b"
    echo "  installed $BINDIR/$b"
done

# ── Start and verify ────────────────────────────────────────────────────────
say "Starting the node"
sudo systemctl start "$SERVICE"
for _ in $(seq 1 60); do $CLI getblockcount >/dev/null 2>&1 && break; sleep 2; done

SUBVER_AFTER=$($CLI getnetworkinfo | grep -o '/BitFinite:[^"]*' || true)
HEIGHT_AFTER=$($CLI getblockcount)
echo "  subversion: $SUBVER_BEFORE  ->  $SUBVER_AFTER"
echo "  height    : $HEIGHT_BEFORE  ->  $HEIGHT_AFTER"

grep -q "${VERSION#v}" <<<"$SUBVER_AFTER" || { echo "node did not come up as $VERSION — run --rollback" >&2; exit 1; }
[ "$HEIGHT_AFTER" -ge "$HEIGHT_BEFORE" ] || { echo "height went backwards — run --rollback" >&2; exit 1; }

say "Starting dependents"
for ((i=${#DEPS[@]}-1; i>=0; i--)); do sudo systemctl start "${DEPS[i]}"; echo "  started ${DEPS[i]}"; done

say "Post-checks"
sleep 10
$CLI getnetworkinfo   | grep -E '"version"|subversion|connections'
$CLI getblockchaininfo | grep -E '"blocks"|verificationprogress|warnings'
for u in "${DEPS[@]}"; do printf '  %-24s %s\n' "$u" "$(systemctl is-active "$u")"; done

say "$(hostname) is on $VERSION. Rollback: $0 --rollback"
