#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: repair_z2m_waterer_state.sh <zigbee2mqtt-data-dir> [device_ieee]

Backs up and removes a stale plant waterer device entry from Zigbee2MQTT's
database and state files. This is intended for endpoint-shape migrations where
Zigbee2MQTT can hang or stay offline while replaying stale metadata.

Arguments:
  zigbee2mqtt-data-dir  Path to Zigbee2MQTT data directory
  device_ieee           Device IEEE address to remove
                        Default: 0x1051dbfffe0d375c

Example:
  ./scripts/repair_z2m_waterer_state.sh ~/homeassistant/zigbee2mqtt-data

Notes:
  - Stop Zigbee2MQTT before running this script.
  - This removes only the specified device entry.
  - Existing files are backed up with a timestamp suffix first.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage >&2
    exit 1
fi

data_dir=$1
device_ieee=${2:-0x1051dbfffe0d375c}

database_file="${data_dir}/database.db"
state_file="${data_dir}/state.json"

if [[ ! -d "${data_dir}" ]]; then
    echo "Data directory not found: ${data_dir}" >&2
    exit 1
fi

if [[ ! -f "${database_file}" ]]; then
    echo "database.db not found: ${database_file}" >&2
    exit 1
fi

if [[ ! -f "${state_file}" ]]; then
    echo "state.json not found: ${state_file}" >&2
    exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "jq is required but not installed." >&2
    exit 1
fi

timestamp=$(date +%Y%m%d-%H%M%S)
database_backup="${database_file}.bak-${timestamp}"
state_backup="${state_file}.bak-${timestamp}"

cp "${database_file}" "${database_backup}"
cp "${state_file}" "${state_backup}"

grep -v "${device_ieee}" "${database_file}" > "${database_file}.new"
mv "${database_file}.new" "${database_file}"

DEVICE_IEEE="${device_ieee}" jq 'del(.[env.DEVICE_IEEE])' "${state_file}" > "${state_file}.new"
mv "${state_file}.new" "${state_file}"

echo "Removed stale Zigbee2MQTT entry for ${device_ieee}"
echo "database backup: ${database_backup}"
echo "state backup: ${state_backup}"
