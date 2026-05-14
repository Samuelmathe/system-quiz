#!/usr/bin/env bash
# Tableau de scores — liaison PC : COM du dongle USB-TTL vers Serial3 Mega (14/15), 9600.
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

exec "$PYTHON" "$ROOT/interface/interface_30eq.py" "$@"
