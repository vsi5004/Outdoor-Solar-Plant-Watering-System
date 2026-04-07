#!/usr/bin/env bash
# Run host-based Unity tests inside the official ESP-IDF Docker image.
# Requires Docker with Compose. No local ESP-IDF install needed.
#
# Usage:
#   bash scripts/run_host_tests.sh
#
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

# On Windows/Git Bash, COMPOSE_CONVERT_WINDOWS_PATHS ensures volume mounts
# resolve correctly through Docker Desktop.
# MSYS_NO_PATHCONV=1 prevents Git Bash from mangling the /project path inside
# the container command.
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    export COMPOSE_CONVERT_WINDOWS_PATHS=1
fi

# Tear down any container left over from a previously interrupted run.
# This handles the case where the host process was SIGKILLed (uncatchable),
# which would have orphaned the container before cleanup could run.
docker compose down --remove-orphans 2>/dev/null || true

# Bring down on any catchable exit (normal finish, Ctrl-C, SIGTERM, HUP).
cleanup() {
    docker compose down --remove-orphans 2>/dev/null || true
}
trap cleanup EXIT INT TERM HUP

echo ">>> Running host tests (docker compose)"
echo ">>> Project root: ${PROJECT_ROOT}"
echo ""

MSYS_NO_PATHCONV=1 docker compose run --rm -T host-tests
