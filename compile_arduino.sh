#!/usr/bin/env bash
# Compile les sketches .ino du projet (arduino-cli).
# Prérequis : arduino-cli dans PATH ou /tmp/arduino-cli (voir ci-dessous).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ARDUINO_CLI_CONFIG_FILE="$ROOT/.arduino-cli/arduino-cli.yaml"

if command -v arduino-cli >/dev/null 2>&1; then
  CLI=arduino-cli
elif [[ -x /tmp/arduino-cli ]]; then
  CLI=/tmp/arduino-cli
else
  echo "arduino-cli introuvable. Installer :" >&2
  echo '  curl -fsSL https://downloads.arduino.cc/arduino-cli/arduino-cli_1.2.2_Linux_64bit.tar.gz | tar -xz -C /tmp' >&2
  echo "Puis : RF24 et DFRobotDFPlayerMini dans ~/Arduino/libraries (arduino-cli lib install ...)" >&2
  exit 1
fi

LIBS="$ROOT/build/libraries,$HOME/Arduino/libraries"
DMX="$ROOT/build/libraries/DMXSerial"
if [[ ! -f "$DMX/library.properties" ]]; then
  mkdir -p "$DMX/src"
  cp "$ROOT/src/DMXSerial.h" "$ROOT/src/DMXSerial.cpp" "$DMX/src/"
  [[ -f "$ROOT/lib/DMXSerial/src/DMXSerial_avr.h" ]] && cp "$ROOT/lib/DMXSerial/src/DMXSerial_avr.h" "$DMX/src/"
  [[ -f "$ROOT/src/DMXSerial_avr.h" ]] && cp "$ROOT/src/DMXSerial_avr.h" "$DMX/src/"
  cat >"$DMX/library.properties" <<'EOF'
name=DMXSerial
version=1.5.0
author=Matthias Hertel
sentence=DMX serial (USART3 sur Mega)
category=Communication
architectures=avr
EOF
fi

compile_one() {
  local name="$1" fqbn="$2"
  local sk="$ROOT/build/sketches/$name"
  mkdir -p "$sk"
  cp "$ROOT/$name.ino" "$sk/"
  echo "—— $name ($fqbn) ——"
  "$CLI" compile --config-file "$ARDUINO_CLI_CONFIG_FILE" -b "$fqbn" --libraries "$LIBS" "$sk"
}

TARGET="${1:-all}"

case "$TARGET" in
  mega)       compile_one megaf arduino:avr:mega ;;
  rf-bridge)  compile_one rf_nano_bridge arduino:avr:nano:cpu=atmega328old ;;
  nano-son)   compile_one nano_son_final arduino:avr:nano:cpu=atmega328old ;;
  nano-eq)    compile_one buzzer_nano_equipe arduino:avr:nano:cpu=atmega328old ;;
  nano-anim)  compile_one nano_animateur_final arduino:avr:nano:cpu=atmega328old ;;
  all)
    compile_one megaf arduino:avr:mega
    compile_one rf_nano_bridge arduino:avr:nano:cpu=atmega328old
    compile_one nano_son_final arduino:avr:nano:cpu=atmega328old
    compile_one buzzer_nano_equipe arduino:avr:nano:cpu=atmega328old
    compile_one nano_animateur_final arduino:avr:nano:cpu=atmega328old
    ;;
  *)
    echo "Usage: $0 [all|mega|rf-bridge|nano-son|nano-eq|nano-anim]" >&2
    exit 1
    ;;
esac

echo "OK — binaires dans build/sketches/<nom>/"
