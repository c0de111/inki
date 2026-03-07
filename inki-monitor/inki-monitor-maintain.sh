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

wait_for_health() {
    local attempts=20
    local delay_s=0.5
    local i
    for ((i = 1; i <= attempts; i++)); do
        if curl -fsS "$HEALTH_URL" >/dev/null 2>&1; then
            return 0
        fi
        sleep "$delay_s"
    done
    return 1
}

if [ "$#" -ne 0 ]; then
    echo "Usage: $(basename "$0")"
    echo
    echo "Maintains the local inki-monitor runtime (backup DB, reinstall Python deps, restart service)."
    echo "It does NOT run git fetch/pull/switch; it only prints current repo status."
    exit 1
fi

need_cmd git
need_cmd curl
need_cmd systemctl
need_cmd date

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null)" || die "script must live inside a git checkout"
VENV_PYTHON="$SCRIPT_DIR/.venv/bin/python"
REQ_FILE="$SCRIPT_DIR/requirements.txt"
DB_FILE="$SCRIPT_DIR/data/inki-monitor.sqlite3"
SERVICE_NAME="inki-monitor"
HEALTH_URL="http://127.0.0.1:3004/healthz"

[ -x "$VENV_PYTHON" ] || die "missing venv python: $VENV_PYTHON"
[ -f "$REQ_FILE" ] || die "missing requirements file: $REQ_FILE"
[ -f "$SCRIPT_DIR/config.yaml" ] || log_warn "config.yaml not found in $SCRIPT_DIR (continuing)"

BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"
COMMIT="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"

log_info "Repo status (manual git update only)"
echo "  root   : $REPO_ROOT"
echo "  branch : $BRANCH"
echo "  commit : $COMMIT"
if git -C "$REPO_ROOT" status --short | grep -q .; then
    echo "  state  : dirty"
    git -C "$REPO_ROOT" status --short
else
    echo "  state  : clean"
fi
echo "  note   : this script does not fetch/pull/switch branches"
echo

log_info "Stopping user service (clean update): $SERVICE_NAME"
if systemctl --user is-active --quiet "$SERVICE_NAME"; then
    if ! systemctl --user stop "$SERVICE_NAME"; then
        print_recent_logs
        die "failed to stop service"
    fi
    log_ok "Service stopped"
else
    log_warn "service is not active (continuing)"
fi
echo

if [ -f "$DB_FILE" ]; then
    BACKUP_FILE="$HOME/inki-monitor-$(date +%Y%m%d-%H%M%S).sqlite3"
    log_info "Backing up SQLite DB after service stop"
    echo "  source : $DB_FILE"
    echo "  target : $BACKUP_FILE"
    "$VENV_PYTHON" - "$DB_FILE" "$BACKUP_FILE" <<'PY' || die "sqlite backup failed"
import sqlite3
import sys

src_path = sys.argv[1]
dst_path = sys.argv[2]

src = sqlite3.connect(src_path)
try:
    dst = sqlite3.connect(dst_path)
    try:
        src.backup(dst)
    finally:
        dst.close()
finally:
    src.close()
PY
    log_ok "Backup completed"
    echo
else
    log_warn "No DB file found at $DB_FILE (skipping backup)"
    echo
fi

log_info "Updating Python dependencies in existing venv"
"$VENV_PYTHON" -m pip install -r "$REQ_FILE" || die "pip install failed"
echo

log_info "Starting user service: $SERVICE_NAME"
if ! systemctl --user start "$SERVICE_NAME"; then
    print_recent_logs
    die "service start failed"
fi

log_info "Health check (retrying briefly while Flask binds): $HEALTH_URL"
if ! wait_for_health; then
    print_recent_logs
    die "health check failed"
fi

log_ok "inki-monitor maintenance completed successfully."
