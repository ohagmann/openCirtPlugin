#!/usr/bin/env bash
# ===========================================================
# Clean Build - Linux (Gegenstueck zu CLEAN_BUILD.bat)
# ===========================================================
# Baut opencirt-<Version>.lrx fuer BricsCAD V26 unter Linux. Die Version
# stammt aus dem obersten Abschnitt von CHANGELOG.md.
#
# Vorgaben lassen sich ueber Umgebungsvariablen aendern:
#   QT6_DIR       Qt-SDK, muss zur Qt-Version von BricsCAD passen (V26: 6.8.2)
#   BRICSCAD_DIR  BricsCAD-Installation (Vorgabe /opt/bricsys/bricscad/v26)
#   OC_SYSROOT    optional: lokal entpackte Entwicklerpakete (OpenGL-Header)
#   OC_TOOLCHAIN  Ordner mit venv/bin/cmake und venv/bin/ninja
#                 (Vorgabe ~/.local/opt/opencirt-toolchain)
#
# Ein laufendes BricsCAD wird NICHT beendet. Es behaelt die bereits geladene
# Fassung des Plugins; die neue gilt nach dem naechsten Start von BricsCAD.

set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"
ROOT="$(pwd)"
BUILD_DIR="$ROOT/build_linux"

echo
echo "============================================================"
echo "Clean Build - opencirt-<Version>.lrx (Linux)"
echo "============================================================"
echo

# Werkzeuge: cmake und ninja aus der lokalen Toolchain, falls vorhanden
OC_TOOLCHAIN="${OC_TOOLCHAIN:-$HOME/.local/opt/opencirt-toolchain}"
if [ -d "$OC_TOOLCHAIN/venv/bin" ]; then
    export PATH="$OC_TOOLCHAIN/venv/bin:$PATH"
fi
for tool in cmake ninja c++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "BUILD ABGEBROCHEN - '$tool' nicht gefunden."
        echo "cmake und ninja lassen sich ohne Systemrechte einrichten:"
        echo "  python3 -m venv \"$OC_TOOLCHAIN/venv\""
        echo "  \"$OC_TOOLCHAIN/venv/bin/pip\" install cmake ninja"
        exit 1
    fi
done

# Step 1: Build-Verzeichnis leeren
echo "[STEP 1] Build-Verzeichnis leeren..."
if [ -d "$BUILD_DIR" ]; then
    rm -rf "${BUILD_DIR:?}"
    echo "  Entfernt: build_linux"
fi
mkdir -p "$BUILD_DIR"
echo "  Angelegt: build_linux"
echo

# Step 2: CMake-Konfiguration
echo "============================================================"
echo "[STEP 2] CMake-Konfiguration"
echo "============================================================"
if ! cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S "$ROOT" -B "$BUILD_DIR"; then
    echo
    echo "CMAKE-KONFIGURATION FEHLGESCHLAGEN"
    exit 1
fi
echo

# Step 3: Bauen
echo "============================================================"
echo "[STEP 3] Bauen"
echo "============================================================"
if ! cmake --build "$BUILD_DIR"; then
    echo
    echo "============================================================"
    echo "BUILD FEHLGESCHLAGEN - Fehler siehe oben"
    echo "============================================================"
    exit 1
fi

echo
echo "============================================================"
echo "BUILD ERFOLGREICH"
echo "============================================================"
echo
LRX=$(ls -1 "$BUILD_DIR"/Release/opencirt-*.lrx 2>/dev/null | head -1)
echo "Ausgabe: ${LRX#$ROOT/}"
ls -l "$LRX"
echo "Kopie im Beispielprojekt: sample_project/00- BricsCAD Plugin/01- Linux Version/$(basename "$LRX")"
echo
echo "Laden in BricsCAD: APPLOAD, dann $(basename "$LRX") waehlen. Befehl: OPENCIRT (Kurzform OC)"
echo "Steht die Datei in der Startup Suite, dort den Eintrag auf die neue Version umstellen."
