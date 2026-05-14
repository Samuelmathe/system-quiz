#!/usr/bin/env bash
# Configuration DMX / équipes — même liaison PC que le quiz : COM du dongle USB-TTL
# vers Serial3 sur la Mega (TX3=14, RX3=15), 9600 baud.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PYTHON="$ROOT/.venv/bin/python"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON=python3
else
  echo "Erreur: python3 introuvable." >&2
  exit 1
fi

exec "$PYTHON" "$ROOT/configuration/config_final_30.py" "$@"
