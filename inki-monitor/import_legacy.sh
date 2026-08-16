#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  import_legacy.sh [--dry-run] --db <sqlite.db> --file <legacy_queries.txt> --device-id <inki-...> --label <label>

Example:
  ./import_legacy.sh --dry-run --db ./data/inki-monitor.sqlite3 --file /path/to/legacy-logs/103H_queries.txt --device-id inki-2CCF67D929A3 --label 103H_queries.txt

Arguments:
  --dry-run            Parse/report only (no DB write)
  --db <sqlite.db>     inki-monitor SQLite DB path
  --file <legacy...>   One legacy `*_queries.txt` file
  --device-id <id>     Target inki device ID (`inki-<MAC>`)
  --label <label>      Stored label (e.g. full legacy filename)

Notes:
  - Imports into `samples` only (keeps `devices` summary rows unchanged)
  - Marks rows as `remote_addr='legacy-import'` and stores provenance in `payload_json`
EOF
}

die() {
  echo "Error: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

sql_quote() {
  local s=${1//\'/\'\'}
  printf "'%s'" "$s"
}

is_int() {
  [[ ${1:-} =~ ^[0-9]+$ ]]
}

DRY_RUN=0
DB_PATH=""
LEGACY_FILE=""
DEVICE_ID=""
LABEL=""

if [[ $# -eq 0 ]]; then
  usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --db)
      [[ $# -ge 2 ]] || die "--db requires a path"
      DB_PATH=$2
      shift 2
      ;;
    --file)
      [[ $# -ge 2 ]] || die "--file requires a path"
      LEGACY_FILE=$2
      shift 2
      ;;
    --device-id)
      [[ $# -ge 2 ]] || die "--device-id requires a value"
      DEVICE_ID=$2
      shift 2
      ;;
    --label)
      [[ $# -ge 2 ]] || die "--label requires a value"
      LABEL=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

require_cmd sqlite3
require_cmd awk

[[ -n "$DB_PATH" ]] || die "--db is required"
[[ -n "$LEGACY_FILE" ]] || die "--file is required"
[[ -n "$DEVICE_ID" ]] || die "--device-id is required"
[[ -n "$LABEL" ]] || die "--label is required"

[[ -r "$DB_PATH" ]] || die "database not readable: $DB_PATH"
[[ -r "$LEGACY_FILE" ]] || die "legacy file not readable: $LEGACY_FILE"
[[ "$DEVICE_ID" =~ ^inki-[0-9A-F]{12}$ ]] || die "device_id must match inki-<12 uppercase hex>"

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
PARSED_TSV="$TMP_DIR/parsed.tsv"
INVALID_TSV="$TMP_DIR/invalid.tsv"
IMPORT_SQL="$TMP_DIR/import.sql"

awk -v out="$PARSED_TSV" -v bad="$INVALID_TSV" '
  BEGIN { OFS="\t" }
  {
    raw = $0
    if (match(raw, /^([0-9]{4})-([0-9]{2})-([0-9]{2})[[:space:]]+([0-9]{2}):([0-9]{2}):([0-9]{2})[[:space:]]*'\''([0-9]+(\.[0-9]+)?)'\''[[:space:]]*$/, m)) {
      ts = sprintf("%s-%s-%s %s:%s:%s", m[1], m[2], m[3], m[4], m[5], m[6])
      epoch = mktime(sprintf("%d %d %d %d %d %d", m[1], m[2], m[3], m[4], m[5], m[6]))
      if (epoch > 0) {
        print NR, epoch, m[7], ts >> out
        next
      }
    }
    print NR, raw >> bad
  }
' "$LEGACY_FILE"

VALID_COUNT=$(wc -l < "$PARSED_TSV" | tr -d ' ')
INVALID_COUNT=0
if [[ -f "$INVALID_TSV" ]]; then
  INVALID_COUNT=$(wc -l < "$INVALID_TSV" | tr -d ' ')
fi

[[ "$VALID_COUNT" -gt 0 ]] || die "no valid legacy lines parsed from: $LEGACY_FILE"

DEVICE_EXISTS=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM devices WHERE device_id = $(sql_quote "$DEVICE_ID");")
is_int "$DEVICE_EXISTS" || die "failed to query devices table in DB"
[[ "$DEVICE_EXISTS" -gt 0 ]] || die "target device_id not found in devices table: $DEVICE_ID"

CURRENT_LABEL=$(sqlite3 "$DB_PATH" "SELECT COALESCE(label,'') FROM devices WHERE device_id = $(sql_quote "$DEVICE_ID") LIMIT 1;")

read -r FIRST_EPOCH FIRST_V FIRST_TS LAST_EPOCH LAST_V LAST_TS MIN_V MAX_V < <(
  awk '
    BEGIN { first=1 }
    {
      epoch=$2 + 0
      v=$3 + 0
      ts=$4
      if (first) {
        first=0
        first_epoch=epoch; first_v=v; first_ts=ts
        min_v=v; max_v=v
      }
      if (v < min_v) min_v=v
      if (v > max_v) max_v=v
      last_epoch=epoch; last_v=v; last_ts=ts
    }
    END {
      printf "%d %.6f %s %d %.6f %s %.6f %.6f\n",
             first_epoch, first_v, first_ts, last_epoch, last_v, last_ts, min_v, max_v
    }
  ' "$PARSED_TSV"
)

DUPLICATE_COUNT=0
while IFS=$'\t' read -r line_no epoch voltage ts; do
  _unused_line_no=$line_no
  _unused_ts=$ts
  count=$(sqlite3 "$DB_PATH" \
    "SELECT COUNT(*) FROM samples
      WHERE device_id = $(sql_quote "$DEVICE_ID")
        AND received_at_unix_s = $epoch
        AND COALESCE(label,'') = $(sql_quote "$LABEL")
        AND COALESCE(remote_addr,'') = 'legacy-import'
        AND battery_before_wifi_v = $voltage;")
  DUPLICATE_COUNT=$((DUPLICATE_COUNT + count))
done < "$PARSED_TSV"

echo "Legacy import target:"
echo "  file         : $LEGACY_FILE"
echo "  device_id    : $DEVICE_ID"
echo "  label        : $LABEL"
echo "  db           : $DB_PATH"
echo "  current label: ${CURRENT_LABEL:-<empty>}"
echo
echo "Parsed summary:"
echo "  valid lines  : $VALID_COUNT"
echo "  invalid lines: $INVALID_COUNT"
echo "  first        : $FIRST_TS  (V=$FIRST_V)"
echo "  last         : $LAST_TS  (V=$LAST_V)"
echo "  voltage range: $MIN_V .. $MAX_V V"
echo "  duplicates   : $DUPLICATE_COUNT (same device_id + timestamp + voltage + label + remote_addr)"
echo
echo "Import behavior:"
echo "  - inserts into samples only"
echo "  - leaves devices summary row unchanged"
echo "  - writes remote_addr='legacy-import'"

if [[ "$INVALID_COUNT" -gt 0 ]]; then
  echo
  echo "First invalid lines (up to 5):"
  head -5 "$INVALID_TSV" | sed 's/^/  /'
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo
  echo "Dry run only. No DB changes made."
  exit 0
fi

DB_Q=$(sql_quote "$DEVICE_ID")
LABEL_Q=$(sql_quote "$LABEL")
REMOTE_Q=$(sql_quote "legacy-import")
FILE_PATH_Q=$(sql_quote "$LEGACY_FILE")
PRE_IMPORT_COUNT=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM samples WHERE device_id = $DB_Q AND COALESCE(label,'') = $LABEL_Q AND COALESCE(remote_addr,'') = 'legacy-import';")

{
  echo "BEGIN IMMEDIATE;"
  awk -v dev_q="$DB_Q" -v label_q="$LABEL_Q" -v remote_q="$REMOTE_Q" -v file_q="$FILE_PATH_Q" '
    {
      line_no = $1
      epoch = $2
      voltage = $3
      ts = $4 " " $5

      gsub(/\047/, "\047\047", ts)

      printf "INSERT INTO samples (device_id, received_at_unix_s, label, use_case, room_type, rtc_backend, fw_version, query_ok, wifi_rssi_dbm, telemetry_send_elapsed_ms, battery_before_wifi_v, battery_after_wifi_v, battery_sag_v, coin_cell_v, pico_temp_c, payload_json, remote_addr) "
      printf "SELECT %s, %s, %s, NULL, NULL, NULL, NULL, NULL, NULL, NULL, %s, NULL, NULL, NULL, NULL, ", dev_q, epoch, label_q, voltage
      printf "json_object('\''source'\'','\''legacy-import'\'','\''legacy_file'\'',%s,'\''legacy_label'\'',%s,'\''legacy_line'\'',%d,'\''legacy_ts'\'','\''%s'\'','\''metrics'\'',json_object('\''battery_before_wifi_v'\'',%s)), %s ", file_q, label_q, line_no, ts, voltage, remote_q
      printf "WHERE NOT EXISTS (SELECT 1 FROM samples WHERE device_id=%s AND received_at_unix_s=%s AND COALESCE(label,'\'''\'')=%s AND COALESCE(remote_addr,'\'''\'')='\''legacy-import'\'' AND battery_before_wifi_v=%s);\n", dev_q, epoch, label_q, voltage
    }
  ' "$PARSED_TSV"
  echo "COMMIT;"
} > "$IMPORT_SQL"

sqlite3 "$DB_PATH" < "$IMPORT_SQL"

POST_DUPLICATES=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM samples WHERE device_id = $DB_Q AND COALESCE(label,'') = $LABEL_Q AND COALESCE(remote_addr,'') = 'legacy-import';")
INSERTED_COUNT=$((POST_DUPLICATES - PRE_IMPORT_COUNT))

echo
echo "Import completed."
echo "  inserted rows (estimated): $INSERTED_COUNT"
echo "  imported rows for this device+label (legacy-import): $POST_DUPLICATES"
