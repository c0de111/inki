#!/usr/bin/env bash
set -u
set -o pipefail

if [ -t 1 ]; then
    C_RED="\033[1;31m"
    C_GREEN="\033[1;32m"
    C_YELLOW="\033[1;33m"
    C_BLUE="\033[1;34m"
    C_RESET="\033[0m"
else
    C_RED=""
    C_GREEN=""
    C_YELLOW=""
    C_BLUE=""
    C_RESET=""
fi

log_info() { echo -e "${C_BLUE}[INFO]${C_RESET} $*"; }
log_ok() { echo -e "${C_GREEN}[OK]${C_RESET} $*"; }
log_warn() { echo -e "${C_YELLOW}[WARN]${C_RESET} $*"; }
log_err() { echo -e "${C_RED}[ERROR]${C_RESET} $*" >&2; }

die() {
    log_err "$*"
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

print_recent_logs() {
    echo
    log_warn "Recent inki-monitor logs:"
    journalctl --user -u "$SERVICE_NAME" -n 8 --no-pager || true
}

print_db_size_line() {
    local f="$1"
    [ -e "$f" ] || return 0
    local bytes
    bytes="$(stat -c%s "$f" 2>/dev/null || echo 0)"
    local human
    human="$(du -h "$f" 2>/dev/null | awk '{print $1}')"
    printf "  %-24s %12s  (%s)\n" "$(basename "$f")" "$human" "${bytes} B"
}

if [ "$#" -ne 0 ]; then
    echo "Usage: $(basename "$0")"
    echo
    echo "Read-only status check for local inki-monitor (repo, service, health, DB, venv deps)."
    echo "No git fetch/pull/switch and no service/database changes are performed."
    exit 1
fi

need_cmd git
need_cmd curl
need_cmd systemctl
need_cmd stat
need_cmd du
need_cmd awk

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null)" || die "script must live inside a git checkout"
SERVICE_NAME="inki-monitor"
HEALTH_URL="http://127.0.0.1:3004/healthz"
DB_FILE="$SCRIPT_DIR/data/inki-monitor.sqlite3"
DB_WAL="$DB_FILE-wal"
DB_SHM="$DB_FILE-shm"
VENV_PYTHON="$SCRIPT_DIR/.venv/bin/python"

BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")"
COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo "unknown")"

log_info "Repo status (read-only)"
echo "  root   : $REPO_ROOT"
echo "  branch : $BRANCH"
echo "  commit : $COMMIT"
if git -C "$REPO_ROOT" status --short | grep -q .; then
    echo "  state  : dirty"
    git -C "$REPO_ROOT" status --short
else
    echo "  state  : clean"
fi
echo

log_info "User service status: $SERVICE_NAME"
ENABLED_STATE="$(systemctl --user is-enabled "$SERVICE_NAME" 2>/dev/null || true)"
ACTIVE_STATE="$(systemctl --user is-active "$SERVICE_NAME" 2>/dev/null || true)"
[ -n "$ENABLED_STATE" ] || ENABLED_STATE="unknown"
[ -n "$ACTIVE_STATE" ] || ACTIVE_STATE="unknown"
echo "  enabled: $ENABLED_STATE"
echo "  active : $ACTIVE_STATE"
if [ "$ACTIVE_STATE" = "active" ]; then
    log_ok "Service is active"
else
    log_warn "Service is not active"
fi
echo

log_info "Health check: $HEALTH_URL"
if HEALTH_BODY="$(curl -fsS "$HEALTH_URL" 2>/dev/null)"; then
    echo "  response: $HEALTH_BODY"
    log_ok "Health check passed"
else
    log_warn "Health check failed"
    if [ "$ACTIVE_STATE" = "active" ] || [ "$ACTIVE_STATE" = "activating" ]; then
        print_recent_logs
    fi
fi
echo

log_info "Python venv"
if [ -x "$VENV_PYTHON" ]; then
    echo "  python : $VENV_PYTHON"
    echo "  version: $("$VENV_PYTHON" --version 2>&1)"
    log_ok "Venv python found"
else
    echo "  python : $VENV_PYTHON"
    log_warn "Venv python missing (skipping DB row count and package checks)"
fi
echo

log_info "SQLite files"
if [ -e "$DB_FILE" ] || [ -e "$DB_WAL" ] || [ -e "$DB_SHM" ]; then
    print_db_size_line "$DB_FILE"
    print_db_size_line "$DB_WAL"
    print_db_size_line "$DB_SHM"
else
    log_warn "No SQLite files found in $SCRIPT_DIR/data"
fi

if [ -x "$VENV_PYTHON" ] && [ -f "$DB_FILE" ]; then
    DB_COUNTS="$("$VENV_PYTHON" - "$DB_FILE" <<'PY' 2>/dev/null || true
import sqlite3
import sys

db = sys.argv[1]
conn = sqlite3.connect(db)
try:
    cur = conn.cursor()
    cur.execute("SELECT COUNT(*) FROM devices")
    devices = cur.fetchone()[0]
    cur.execute("SELECT COUNT(*) FROM samples")
    samples = cur.fetchone()[0]
    print(f"{devices}\t{samples}")
finally:
    conn.close()
PY
)"
    if [ -n "$DB_COUNTS" ]; then
        DEVICES_COUNT="${DB_COUNTS%%$'\t'*}"
        SAMPLES_COUNT="${DB_COUNTS##*$'\t'}"
        echo "  devices rows: $DEVICES_COUNT"
        echo "  samples rows: $SAMPLES_COUNT"
    else
        log_warn "Could not read DB row counts"
    fi
fi
echo

log_info "Python packages (outdated within current venv)"
if [ -x "$VENV_PYTHON" ]; then
    OUTDATED_JSON="$("$VENV_PYTHON" -m pip list --outdated --format=json 2>/dev/null || true)"
    if [ -z "$OUTDATED_JSON" ]; then
        log_warn "Could not determine outdated packages (network unavailable or pip error)"
    else
        "$VENV_PYTHON" - "$OUTDATED_JSON" <<'PY'
import json
import sys

raw = sys.argv[1]
try:
    items = json.loads(raw)
except Exception:
    print("  status : unable to parse pip output")
    sys.exit(0)

print(f"  outdated count: {len(items)}")
for item in items[:10]:
    name = item.get("name", "?")
    ver = item.get("version", "?")
    latest = item.get("latest_version", "?")
    print(f"  - {name}: {ver} -> {latest}")
if len(items) > 10:
    print(f"  ... {len(items) - 10} more")
PY
        log_ok "Outdated-package check completed"
    fi
else
    log_warn "Skipping package check (venv python missing)"
fi
echo

log_ok "inki-monitor check completed."
