#!/usr/bin/env bash
# ==========================================================================
# Prepare le dossier www/ de l'app Capacitor Regie/Config a partir de web/
# (source de verite unique, voir sync_web_to_esp32.sh a la racine pour le
# meme principe cote ESP32/LittleFS).
# ==========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="$ROOT/web"
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WWW_DIR="$APP_DIR/www"

echo "📂 Preparation de $WWW_DIR (Regie/Config)..."
rm -rf "$WWW_DIR"
mkdir -p "$WWW_DIR"

# Page unique : config.html devient le point d'entree de cette app dediee,
# separee de l'app Animateur+Public pour ne pas exposer l'outil de regie
# (voir aussi le mot de passe CONFIG_PASSWORD, deuxieme niveau de protection
# cote ESP32) a tout le monde par defaut.
cp "$WEB_DIR/config.html" "$WWW_DIR/index.html"
cp "$WEB_DIR/config.js" "$WWW_DIR/config.js"
cp "$WEB_DIR/app-common.js" "$WWW_DIR/app-common.js"
cp "$WEB_DIR/style.css" "$WWW_DIR/style.css"

echo "✅ www/ pret. Lancez ensuite : npm install && npm run init:android && npm run sync && npm run build:apk"
