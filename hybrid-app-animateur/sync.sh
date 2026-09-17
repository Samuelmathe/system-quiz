#!/usr/bin/env bash
# ==========================================================================
# Prepare le dossier www/ de l'app Capacitor Animateur+Public a partir de
# web/ (source de verite unique, voir sync_web_to_esp32.sh a la racine pour
# le meme principe cote ESP32/LittleFS).
# ==========================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEB_DIR="$ROOT/web"
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WWW_DIR="$APP_DIR/www"

echo "📂 Preparation de $WWW_DIR (Animateur + Public)..."
rm -rf "$WWW_DIR"
mkdir -p "$WWW_DIR/vendor"

# Portail specifique a cette app (2 cartes : Animateur / Public) comme point
# d'entree, animateur.html/public.html gardent leur nom (pas de lien croise
# entre eux dans le code source, donc renommer casserait rien mais autant
# rester identique a web/ pour eviter toute divergence).
cp "$APP_DIR/index-portal-source.html" "$WWW_DIR/index.html"
cp "$WEB_DIR/animateur.html" "$WWW_DIR/animateur.html"
cp "$WEB_DIR/animateur.js" "$WWW_DIR/animateur.js"
cp "$WEB_DIR/public.html" "$WWW_DIR/public.html"
cp "$WEB_DIR/app-common.js" "$WWW_DIR/app-common.js"
cp "$WEB_DIR/style.css" "$WWW_DIR/style.css"
cp "$WEB_DIR/manifest.json" "$WWW_DIR/manifest.json"
cp "$WEB_DIR/vendor/xlsx.full.min.js" "$WWW_DIR/vendor/xlsx.full.min.js"

echo "✅ www/ pret. Lancez ensuite : npm install && npm run init:android && npm run sync && npm run build:apk"
