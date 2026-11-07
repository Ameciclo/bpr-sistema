#!/usr/bin/env bash
# Verificação nativa (host) das correções de fixes/november.
# Compila os fontes REAIS do firmware com stubs de host e sanitizadores
# (ASan+UBSan) — evidência E1/E1-L no padrão da revisão. Não substitui placa.
set -euo pipefail
cd "$(dirname "$0")"

ROOT="$(cd ../.. && pwd)"
AJ="$ROOT/firmware/central/.pio/libdeps/seeed_xiao_esp32c3/ArduinoJson/src"
CRC="$ROOT/firmware/central/.pio/libdeps/seeed_xiao_esp32c3/CRC32/src"
CENTRAL_INC="-I$ROOT/firmware/central/include -I$ROOT/firmware/common"
BICI_INC="-I$ROOT/firmware/bici/include -I$ROOT/firmware/common"
STUBS="-Istubs"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
CXXFLAGS="-std=gnu++17 -g -O1 $SAN -Wno-unused-parameter"

mkdir -p build

echo "== central_checks =="
g++ $CXXFLAGS $STUBS $CENTRAL_INC -I"$AJ" -I"$CRC" \
    central_checks.cpp \
    "$ROOT/firmware/central/src/buffer_manager.cpp" \
    "$ROOT/firmware/central/src/config_manager.cpp" \
    "$ROOT/firmware/central/src/config_credentials.cpp" \
    "$ROOT/firmware/central/src/bpr_json_helper.cpp" \
    "$CRC/CRC32.cpp" \
    -o build/central_checks 2> build/central_checks-build.log || {
      echo "BUILD FAILED"; cat build/central_checks-build.log; exit 1; }
./build/central_checks 2> build/central_checks-sanitizer.log | tee central-checks-results.txt
test ! -s build/central_checks-sanitizer.log || { echo "SANITIZER OUTPUT:"; cat build/central_checks-sanitizer.log; exit 1; }
# O08: valores impressos viram critério de reprovação — não só evidência
! grep -q "no(BUG)" central-checks-results.txt || { echo "CENTRAL CHECKS FAILED (no(BUG))"; exit 1; }

echo "== bici_checks =="
g++ $CXXFLAGS $STUBS $BICI_INC -I"$AJ" \
    bici_checks.cpp \
    "$ROOT/firmware/bici/src/buffer_manager.cpp" \
    "$ROOT/firmware/bici/src/config_manager.cpp" \
    "$ROOT/firmware/bici/src/power_manager.cpp" \
    "$ROOT/firmware/bici/src/scanning.cpp" \
    "$ROOT/firmware/bici/src/utils.cpp" \
    -o build/bici_checks 2> build/bici_checks-build.log || {
      echo "BUILD FAILED"; cat build/bici_checks-build.log; exit 1; }
./build/bici_checks 2> build/bici_checks-sanitizer.log | tee bici-checks-results.txt
test ! -s build/bici_checks-sanitizer.log || { echo "SANITIZER OUTPUT:"; cat build/bici_checks-sanitizer.log; exit 1; }
# O08: idem — qualquer no(BUG) reprova o runner
! grep -q "no(BUG)" bici-checks-results.txt || { echo "BICI CHECKS FAILED (no(BUG))"; exit 1; }

echo "== bot_checks (N11/N04 — rideCalculator real + RTDB transacional local) =="
node verify-bot.js | tee bot-checks-results.txt
grep -q "sem BUG" bot-checks-results.txt || { echo "BOT CHECKS FAILED"; exit 1; }

echo
echo "OK: todas as verificações rodaram sem achado de sanitizador"
