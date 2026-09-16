#!/usr/bin/env bash
# ==========================================================================
# Synchronisation des fichiers Web vers le dossier data/ de l'ESP32 LittleFS
# ==========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$ROOT/web"
DATA_DIR="$ROOT/esp32/data"

echo "📂 Synchronisation de $WEB_DIR vers $DATA_DIR..."
mkdir -p "$DATA_DIR/assets"
cp -ru "$WEB_DIR"/* "$DATA_DIR"/
if [ -d "$ROOT/assets" ]; then
  cp -ru "$ROOT/assets"/* "$DATA_DIR/assets"/
fi

echo "✅ Fichiers synchronisés avec succès !"
echo "Pour téléverser dans l'ESP32 :"
echo "1. Ouvrir 'esp32/esp32_bridge_server.ino' dans l'Arduino IDE"
echo "2. Outils > ESP32 LittleFS Data Upload"
