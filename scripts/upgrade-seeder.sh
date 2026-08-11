#!/usr/bin/env bash
#
# BitFinite DNS seeder — in-place upgrade from a published release.
#
# The seeder lives outside the node's bin directory and is NOT touched by
# scripts/upgrade-node.sh, so it needs its own swap. It serves the network's
# dynamic bootstrap record, and while it is stopped that record answers
# nothing — so this keeps the window as short as it can and reports how long
# it actually was.
#
# Usage:
#   scripts/upgrade-seeder.sh v3.1.0                 # upgrade
#   scripts/upgrade-seeder.sh v3.1.0 --dry-run       # verify only, change nothing
#   scripts/upgrade-seeder.sh --rollback             # restore the previous binary
#
# Config (environment or /etc/bitfinite/upgrade.conf):
#   BFX_SEEDER_BIN   path to the seeder binary   (default: $HOME/bitcoin-seeder)
#   BFX_SEEDER_UNIT  systemd unit name           (default: bfx-seeder)
#   BFX_SEEDER_HOST  hostname it serves, for the post-check DNS query
#                    (default: read from the unit's ExecStart)
#   BFX_REPO / BFX_SHA256 / BFX_CHECKSUMS — as in upgrade-node.sh
#
# Run as the user that owns the binary, from a terminal where sudo can prompt.

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

BFX_CONF="${BFX_CONF:-/etc/bitfinite/upgrade.conf}"
# shellcheck disable=SC1090
[ -r "$BFX_CONF" ] && . "$BFX_CONF"

UNIT="${BFX_SEEDER_UNIT:-bfx-seeder}"
BIN="${BFX_SEEDER_BIN:-$HOME/bitcoin-seeder}"
REPO="${BFX_REPO:-bitfinitechain/bitfinite-core}"
BAK="$BIN.bak-pre-upgrade"

say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
run() { if [ "$DRY" = 1 ]; then echo "  [dry-run] $*"; else eval "$@"; fi; }

# The hostname it serves, so the post-check queries the right record rather than
# a guess. Taken from the unit so it cannot drift from what is actually running.
if [ -z "${BFX_SEEDER_HOST:-}" ]; then
    BFX_SEEDER_HOST=$(systemctl cat "$UNIT" 2>/dev/null \
        | sed -n 's/.*-host=\([^ ]*\).*/\1/p' | head -1 || true)
fi

# ── Rollback ────────────────────────────────────────────────────────────────
if [ "$ROLLBACK" = 1 ]; then
    say "Rolling back the seeder"
    [ -f "$BAK" ] || { echo "no backup at $BAK — cannot roll back" >&2; exit 1; }
    run "sudo systemctl stop $UNIT"
    run "install -m755 '$BAK' '$BIN'"
    run "sudo systemctl start $UNIT"
    run "sleep 5"
    run "systemctl is-active $UNIT"
    say "Rolled back."
    exit 0
fi

[ -n "$VERSION" ] || { echo "usage: $(basename "$0") vX.Y.Z [--dry-run] | --rollback" >&2; exit 2; }
PKG="bitfinite-${VERSION}-x86_64-linux"
URL="https://github.com/${REPO}/releases/download/${VERSION}/${PKG}.tar.gz"
WORK="${TMPDIR:-/tmp}/bfx-seeder-${VERSION}"

# ── Preflight ───────────────────────────────────────────────────────────────
say "Preflight on $(hostname)"
[ -x "$BIN" ] || { echo "no seeder binary at $BIN" >&2; exit 1; }
ACTIVE=$(systemctl is-active "$UNIT" 2>/dev/null || true)
CURVER=$("$BIN" --help 2>&1 | head -1)
echo "  unit        : $UNIT ($ACTIVE)"
echo "  binary      : $BIN"
echo "  current     : $CURVER"
echo "  target      : $VERSION"
echo "  serves      : ${BFX_SEEDER_HOST:-<unknown>}"
echo "  state files : $(ls -la "$HOME"/dnsseed.dat 2>/dev/null | awk '{print $5" bytes"}' || echo 'no dnsseed.dat yet')"

# systemd sends SIGTERM and waits TimeoutStopSec before SIGKILL. Older seeders
# ignore the signal, so `stop` can block for the whole timeout — that is DNS
# bootstrap downtime, so say up front how long it could be.
TMO=$(systemctl show "$UNIT" -p TimeoutStopUSec --value 2>/dev/null || echo '?')
echo "  stop timeout: $TMO (worst-case bootstrap outage if it ignores SIGTERM)"

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
( cd "$PKG" && grep " bitcoin-seeder$" SHA256SUMS | sed 's#^#&#' > /tmp/.bfxseed.$$ \
  && ( cd bin && sha256sum -c /tmp/.bfxseed.$$ ); rm -f /tmp/.bfxseed.$$ )
NEWVER=$("./$PKG/bin/bitcoin-seeder" --help 2>&1 | head -1)
echo "  new binary reports: $NEWVER"
grep -q "${VERSION#v}" <<<"$NEWVER" || { echo "binary does not report $VERSION" >&2; exit 1; }

if [ "$DRY" = 1 ]; then
    say "Dry run complete — verified artefacts, changed nothing."
    exit 0
fi

# ── Back up ─────────────────────────────────────────────────────────────────
# The crawl database is backed up too. Older seeders do not persist on exit, so
# a SIGKILL discards everything crawled since the last periodic dump; the copy
# means a rollback still has a warm database to start from.
say "Backing up binary and crawl database"
STAMP=$(date +%Y%m%d-%H%M%S)
cp -a "$BIN" "$BAK"
for f in dnsseed.dat dnsseed.dump; do
    [ -f "$HOME/$f" ] && cp -a "$HOME/$f" "$HOME/$f.pre-${VERSION}-$STAMP"
done
echo "  binary -> $BAK"
ls "$HOME"/dnsseed.*.pre-"${VERSION}"-"$STAMP" 2>/dev/null | sed 's/^/  db     -> /' || true

# ── Swap ────────────────────────────────────────────────────────────────────
say "Stopping the seeder (bootstrap record is dark from here)"
T0=$(date +%s)
sudo systemctl stop "$UNIT"
T1=$(date +%s)
echo "  stopped after $((T1-T0))s"

install -m755 "$WORK/$PKG/bin/bitcoin-seeder" "$BIN"
echo "  installed $BIN"

sudo systemctl start "$UNIT"
T2=$(date +%s)
echo "  started — total outage $((T2-T0))s"

# ── Verify ──────────────────────────────────────────────────────────────────
say "Post-checks"
sleep 8
printf '  unit     : %s\n' "$(systemctl is-active "$UNIT")"
printf '  version  : %s\n' "$("$BIN" --help 2>&1 | head -1)"

# The whole point of the new build is that a seeder which cannot bind its DNS
# port says so. Surface those warnings rather than trusting "active".
BINDERR=$(journalctl -u "$UNIT" --since "-2 min" --no-pager 2>/dev/null | grep -ci "exited with error" || true)
printf '  DNS bind : %s\n' "$([ "${BINDERR:-0}" -eq 0 ] && echo 'no thread errors' || echo "${BINDERR} THREAD ERROR(S) — check journalctl -u $UNIT")"

# Does it actually answer? "active" proves the process is up, not that DNS works.
if [ -n "${BFX_SEEDER_HOST:-}" ] && command -v dig >/dev/null 2>&1; then
    ANS=$(dig +short +time=3 +tries=1 @127.0.0.1 "$BFX_SEEDER_HOST" A 2>/dev/null | grep -c '^[0-9]' || true)
    printf '  answers  : %s address(es) for %s\n' "${ANS:-0}" "$BFX_SEEDER_HOST"
    [ "${ANS:-0}" -eq 0 ] && echo "  WARNING: seeder is running but returned no addresses" >&2
else
    echo "  answers  : skipped (no dig, or host unknown)"
fi

journalctl -u "$UNIT" -n 3 --no-pager 2>/dev/null | sed 's/^/  /'

say "Seeder is on $VERSION. Rollback: $0 --rollback"
