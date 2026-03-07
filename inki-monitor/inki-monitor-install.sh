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
    echo "Bootstraps inki-monitor in the current checkout (venv, deps, user systemd service, health check)."
    echo "No git fetch/pull/switch is performed."
    exit 1
fi

need_cmd python3
need_cmd systemctl
need_cmd curl
need_cmd cp
need_cmd mkdir
need_cmd sed

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_NAME="inki-monitor"
HEALTH_URL="http://127.0.0.1:3004/healthz"
VENV_DIR="$SCRIPT_DIR/.venv"
VENV_PYTHON="$VENV_DIR/bin/python"
REQ_FILE="$SCRIPT_DIR/requirements.txt"
CONFIG_EXAMPLE="$SCRIPT_DIR/config.example.yaml"
CONFIG_FILE="$SCRIPT_DIR/config.yaml"
SERVICE_TEMPLATE="$SCRIPT_DIR/systemd/inki-monitor.service.example"
USER_SYSTEMD_DIR="$HOME/.config/systemd/user"
USER_SERVICE_FILE="$USER_SYSTEMD_DIR/inki-monitor.service"

[ -f "$REQ_FILE" ] || die "missing requirements file: $REQ_FILE"
[ -f "$CONFIG_EXAMPLE" ] || die "missing config example: $CONFIG_EXAMPLE"
[ -f "$SERVICE_TEMPLATE" ] || die "missing systemd template: $SERVICE_TEMPLATE"

if [ ! -x "$VENV_PYTHON" ]; then
    log_info "Creating Python virtual environment: $VENV_DIR"
    python3 -m venv "$VENV_DIR" || die "failed to create venv"
    log_ok "Venv created"
    echo
else
    log_info "Using existing venv: $VENV_DIR"
    echo
fi

log_info "Installing Python dependencies into venv"
"$VENV_PYTHON" -m pip install -r "$REQ_FILE" || die "pip install failed"
log_ok "Dependencies installed"
echo

if [ ! -f "$CONFIG_FILE" ]; then
    log_info "Creating config from template"
    cp "$CONFIG_EXAMPLE" "$CONFIG_FILE" || die "failed to create config.yaml"
    log_ok "Created: $CONFIG_FILE"
    log_warn "Edit config.yaml (token, host/port as needed) before exposing the service on the LAN."
    echo
else
    log_info "Keeping existing config: $CONFIG_FILE"
    echo
fi

log_info "Installing/updating user systemd service"
mkdir -p "$USER_SYSTEMD_DIR" || die "failed to create $USER_SYSTEMD_DIR"
cp "$SERVICE_TEMPLATE" "$USER_SERVICE_FILE" || die "failed to copy service template"
sed -i \
    -e "s|^WorkingDirectory=.*|WorkingDirectory=$SCRIPT_DIR|" \
    -e "s|^ExecStart=.*|ExecStart=$VENV_PYTHON $SCRIPT_DIR/app.py|" \
    "$USER_SERVICE_FILE" || die "failed to patch service file"
log_ok "User service file updated: $USER_SERVICE_FILE"
echo

log_info "Reloading and starting user service: $SERVICE_NAME"
systemctl --user daemon-reload || die "systemd daemon-reload failed"
if ! systemctl --user enable --now "$SERVICE_NAME"; then
    print_recent_logs
    die "failed to enable/start service"
fi

log_info "Health check (retrying briefly while Flask binds): $HEALTH_URL"
if ! wait_for_health; then
    print_recent_logs
    die "health check failed"
fi

log_ok "inki-monitor install/bootstrap completed successfully."
echo
log_info "Optional (keep user service running after logout):"
echo "  sudo loginctl enable-linger \"\$USER\""
