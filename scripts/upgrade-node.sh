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
# Ask systemd for THIS unit's process before falling back to a name match.
# `pgrep -x bitfinited` returns an arbitrary daemon on a host running more than
# one, which is how seed-4 reported a testnet binary as the running node while
# being asked to upgrade mainnet. The unit knows which process is its own.
PID=$(systemctl show -p MainPID --value "$SERVICE" 2>/dev/null || true)
[ "${PID:-0}" = 0 ] && PID=""
[ -n "$PID" ] || PID=$(pgrep -x bitfinited | head -1 || true)
if [ -n "$PID" ] && [ -r "/proc/$PID/exe" ]; then
    RUNNING_EXE=$(readlink -f "/proc/$PID/exe")
else
    RUNNING_EXE=""
fi
if [ -z "${BFX_BINDIR:-}" ] && [ -z "$RUNNING_EXE" ]; then
    echo "cannot find the running node and BFX_BINDIR is not set; pass BFX_BINDIR" >&2
    exit 1
fi
BINDIR="${BFX_BINDIR:-$(dirname "$RUNNING_EXE")}"

if [ -n "${BFX_DATADIR:-}" ]; then
    DATADIR="$BFX_DATADIR"
else
    DATADIR=""
    [ -n "$PID" ] && [ -r "/proc/$PID/cmdline" ] && \
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
# A stopped node must not block the upgrade. This is exactly the state a failed
# run leaves behind, so demanding a live RPC here made the retry impossible and
# turned one aborted upgrade on seed-4 into a service outage that needed a
# manual systemctl start before the script would even talk to us.
if $CLI getblockcount >/dev/null 2>&1; then
    NODE_WAS_UP=1
    HEIGHT_BEFORE=$($CLI getblockcount)
    SUBVER_BEFORE=$($CLI getnetworkinfo | grep -o '/BitFinite:[^"]*' || true)
    IBD=$($CLI getblockchaininfo | grep -o '"initialblockdownload": *[a-z]*' | awk '{print $2}')
else
    NODE_WAS_UP=0
    HEIGHT_BEFORE="n/a"
    SUBVER_BEFORE="(node not running)"
    IBD="n/a"
    echo "  NOTE: $SERVICE is not running. Upgrading anyway and starting it at the end."
fi
FREE_MB=$(df -Pm "$BINDIR" | awk 'NR==2{print $4}')

echo "  target      : $VERSION"
echo "  running     : ${RUNNING_EXE:-(not running)}"
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

if [ "$NODE_WAS_UP" = 1 ]; then
    [ "$IBD" = false ] || { echo "node is in initial block download, not a good moment" >&2; exit 1; }
fi
[ "$FREE_MB" -ge 500 ] || { echo "need >=500 MB free, have ${FREE_MB} MB" >&2; exit 1; }

for L in "${LOCKS[@]}"; do
    [ -n "$L" ] || continue
    exec {lfd}>"$L"
    flock -n "$lfd" || { echo "could not take $L — a job is running, retry shortly" >&2; exit 1; }
    echo "  holding lock: $L"
done

# ── Fetch and verify ────────────────────────────────────────────────────────
say "Fetching and verifying $PKG"
mkdir -p "$WORK"; cd "$WORK"
[ -f "$PKG.tar.gz" ] || curl -fsSL -o "$PKG.tar.gz" "$URL"

# The archive hash is ADVISORY. A rebuild that only re-tars the same binaries
# changes it — CI does exactly that on tag push — so failing here would reject
# a good artifact. Report the difference and let the binary pins decide.
SHA="${BFX_SHA256:-}"
if [ -z "$SHA" ] && [ -r "$CHECKSUMS" ]; then
    SHA=$(awk -v f="$PKG.tar.gz" '$2==f {print $1}' "$CHECKSUMS" | head -1 || true)
fi
GOT=$(sha256sum "$PKG.tar.gz" | awk '{print $1}')
if [ -n "$SHA" ] && [ "$SHA" = "$GOT" ]; then
    echo "  archive  : matches the pinned hash"
elif [ -n "$SHA" ]; then
    echo "  archive  : differs from the pin (re-packaged upstream?) — binaries decide"
else
    echo "  archive  : no pin recorded — binaries decide"
fi

rm -rf "$PKG"; tar xzf "$PKG.tar.gz"

# The binaries are the AUTHORITATIVE gate: this is what gets executed. Verify
# against hashes pinned in git, NOT against the SHA256SUMS inside the archive —
# that one travels with the artifact and only attests to itself.
say "Verifying binaries against the pinned hashes"
[ -r "$CHECKSUMS" ] || { echo "no checksum file at $CHECKSUMS" >&2; exit 1; }
for b in "${BINS[@]}"; do
    want=$(awk -v k="$VERSION/$b" '$2==k {print $1}' "$CHECKSUMS" | head -1 || true)
    [ -n "$want" ] || { echo "  $b: NO PINNED HASH for $VERSION — refusing" >&2; exit 1; }
    have=$(sha256sum "$PKG/bin/$b" | awk '{print $1}')
    [ "$want" = "$have" ] || { echo "  $b: HASH MISMATCH" >&2; echo "    pinned $want" >&2; echo "    got    $have" >&2; exit 1; }
    echo "  $b: OK"
done

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
# Wait on THIS unit, not on the process name.
#
# This used to poll `pgrep -x bitfinited`, which matches every daemon of that
# name on the host. On a box running more than one node that is a false
# positive that never clears: seed-4 also runs two testnet daemons, so the
# check sat for 60s waiting on nodes that were never meant to stop, then
# aborted the upgrade with the mainnet node and all three pools already down.
# Its own log showed "Shutdown: done" seconds in. Systemd knows which process
# belongs to the unit, so ask it.
for _ in $(seq 1 120); do
    [ "$(systemctl is-active "$SERVICE" 2>/dev/null)" != "active" ] && break
    sleep 1
done
if [ "$(systemctl is-active "$SERVICE" 2>/dev/null)" = "active" ]; then
    echo "$SERVICE did not stop within 120s" >&2; exit 1
fi
# Belt and braces: the unit can be inactive while the datadir lock is still
# held. If RPC still answers on this datadir, the swap is not safe yet.
if $CLI getblockcount >/dev/null 2>&1; then
    echo "$SERVICE reports inactive but its RPC still answers; not swapping" >&2; exit 1
fi
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
if [ "$NODE_WAS_UP" = 1 ]; then
    [ "$HEIGHT_AFTER" -ge "$HEIGHT_BEFORE" ] || { echo "height went backwards, run --rollback" >&2; exit 1; }
fi

say "Starting dependents"
for ((i=${#DEPS[@]}-1; i>=0; i--)); do sudo systemctl start "${DEPS[i]}"; echo "  started ${DEPS[i]}"; done

say "Post-checks"
sleep 10
$CLI getnetworkinfo   | grep -E '"version"|subversion|connections'
$CLI getblockchaininfo | grep -E '"blocks"|verificationprogress|warnings'
for u in "${DEPS[@]}"; do printf '  %-24s %s\n' "$u" "$(systemctl is-active "$u")"; done

say "$(hostname) is on $VERSION. Rollback: $0 --rollback"
