#!/usr/bin/env bash
# ===========================================================
# Windows-Build unter Linux - opencirt-<Version>-v<NN>.brx
# ===========================================================
# Baut die Windows-Fassung ohne Windows-Rechner: clang-cl + lld-link gegen
# MSVC-Bibliotheken und Windows SDK (/winsysroot-Layout, z.B. von xwin) und
# Qt fuer msvc2022_64. moc/uic/rcc kommen vom Linux-Qt derselben Reihe.
#   ./CROSS_BUILD_WINDOWS.sh       fuer V27
#   ./CROSS_BUILD_WINDOWS.sh 26    fuer V26
#
# Vorgaben (Umgebungsvariablen), Grundordner OC_TOOLCHAIN
# (Vorgabe ~/.local/opt/opencirt-toolchain):
#   OC_LLVM_BIN    clang-cl, lld-link, llvm-lib/-rc/-mt
#                  (Vorgabe $OC_TOOLCHAIN/llvm20/usr/lib/llvm-20/bin)
#   OC_WINSYSROOT  MSVC mit ATL + Windows SDK (Vorgabe $OC_TOOLCHAIN/winsysroot)
#   QT6_DIR        Qt fuer Windows (Vorgabe $OC_TOOLCHAIN/Qt-win/<ver>/msvc2022_64,
#                  V26: 6.8.3, V27: 6.10.3)
#   QT_HOST_PATH   Linux-Qt fuer moc/uic/rcc (Vorgabe $OC_TOOLCHAIN/Qt/<ver>/gcc_64,
#                  V26: 6.8.2, V27: 6.10.2)
#   BRX_SDK_DIR    BRX SDK (Vorgabe external/brx_sdk_v<NN>, mit lib64/brx<NN>.lib)
#   OC_JOBS        parallele Uebersetzungen (Vorgabe 4)
#
# Die MSVC-Bibliotheken und das Windows SDK unterliegen der Lizenz von
# Microsoft und gehoeren nicht ins Repo.

set -euo pipefail

case "${1:-}" in
    26|27) BRICSCAD_VERSION="$1" ;;
    "") BRICSCAD_VERSION="${BRICSCAD_VERSION:-27}" ;;
    *) echo "Aufruf: $0 [26|27]"; exit 1 ;;
esac
case "$BRICSCAD_VERSION" in
    26) QT_WIN_VER=6.8.3;  QT_HOST_VER=6.8.2 ;;
    27) QT_WIN_VER=6.10.3; QT_HOST_VER=6.10.2 ;;
    *) echo "BRICSCAD_VERSION=$BRICSCAD_VERSION wird nicht unterstuetzt (26 oder 27)."; exit 1 ;;
esac

cd "$(dirname "$(readlink -f "$0")")"
ROOT="$(pwd)"
BUILD_DIR="$ROOT/build_windows_cross"

OC_TOOLCHAIN="${OC_TOOLCHAIN:-$HOME/.local/opt/opencirt-toolchain}"
export OC_LLVM_BIN="${OC_LLVM_BIN:-$OC_TOOLCHAIN/llvm20/usr/lib/llvm-20/bin}"
export OC_WINSYSROOT="${OC_WINSYSROOT:-$OC_TOOLCHAIN/winsysroot}"
QT6_DIR="${QT6_DIR:-$OC_TOOLCHAIN/Qt-win/$QT_WIN_VER/msvc2022_64}"
QT_HOST_PATH="${QT_HOST_PATH:-$OC_TOOLCHAIN/Qt/$QT_HOST_VER/gcc_64}"
OC_JOBS="${OC_JOBS:-4}"
if [ -d "$OC_TOOLCHAIN/venv/bin" ]; then
    export PATH="$OC_TOOLCHAIN/venv/bin:$PATH"
fi

echo
echo "============================================================"
echo "Windows-Build unter Linux - opencirt-<Version>-v$BRICSCAD_VERSION.brx"
echo "============================================================"
echo

fehlt() {
    echo "BUILD ABGEBROCHEN - $1"
    exit 1
}
for tool in clang-cl lld-link llvm-lib llvm-rc llvm-mt; do
    [ -x "$OC_LLVM_BIN/$tool" ] || fehlt "$tool fehlt in $OC_LLVM_BIN (OC_LLVM_BIN)."
done
[ -d "$OC_WINSYSROOT/VC/Tools/MSVC" ] && [ -d "$OC_WINSYSROOT/Windows Kits/10/Include" ] \
    || fehlt "MSVC/Windows SDK fehlen in $OC_WINSYSROOT (OC_WINSYSROOT), z.B. mit xwin --include-atl splat --use-winsysroot-style --preserve-ms-arch-notation holen."
[ -f "$QT6_DIR/lib/cmake/Qt6/Qt6Config.cmake" ] || fehlt "Qt $QT_WIN_VER msvc2022_64 fehlt: $QT6_DIR (QT6_DIR)."
[ -x "$QT_HOST_PATH/libexec/moc" ] || fehlt "Linux-Qt $QT_HOST_VER fehlt: $QT_HOST_PATH (QT_HOST_PATH)."
for tool in cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || fehlt "'$tool' nicht gefunden."
done

# Step 1: Build-Verzeichnis leeren
echo "[STEP 1] Build-Verzeichnis leeren..."
rm -rf "${BUILD_DIR:?}"
mkdir -p "$BUILD_DIR"
echo "  Angelegt: build_windows_cross"

# Das BRX SDK bindet einige Windows-Header in anderer Schreibweise ein als
# die Dateinamen im Windows SDK; unter Linux zaehlt die Gross-/Kleinschreibung.
# Fehlende Schreibweisen bekommen hier einen Verweis (Ordner nur fuer den Build).
CASE_DIR="$BUILD_DIR/oc_case_alias"
mkdir -p "$CASE_DIR"
SDK_INC=$(ls -d "$OC_WINSYSROOT/Windows Kits/10/Include"/*/ | sort -V | tail -1)
# ATL: xwin legt es zu den MSVC-Headern (include), Visual Studio nach atlmfc/include
ATL_INC=$(dirname "$(find "$OC_WINSYSROOT/VC/Tools/MSVC" -maxdepth 4 -iname atlbase.h -print -quit)")
[ "$ATL_INC" != "." ] || fehlt "ATL fehlt in $OC_WINSYSROOT (xwin --include-atl)."
INC_DIRS=("${SDK_INC}um" "${SDK_INC}shared" "${SDK_INC}ucrt" "$ATL_INC")
for name in Commctrl.h OAIdl.h OleAuto.h AtlBase.h; do
    for d in "${INC_DIRS[@]}"; do
        [ -e "$d/$name" ] && continue 2
    done
    real=$(find "${INC_DIRS[@]}" -maxdepth 1 -iname "$name" -print -quit)
    [ -n "$real" ] || fehlt "$name fehlt im Windows SDK/ATL."
    ln -s "$real" "$CASE_DIR/$name"
    echo "  Verweis: $name -> ${real#$OC_WINSYSROOT/}"
done
export OC_CASE_ALIAS_DIR="$CASE_DIR"
echo

# Step 2: CMake-Konfiguration
echo "============================================================"
echo "[STEP 2] CMake-Konfiguration"
echo "============================================================"
if ! cmake -G Ninja -S "$ROOT" -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/windows-cross.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBRICSCAD_VERSION="$BRICSCAD_VERSION" \
        -DQT6_DIR="$QT6_DIR" \
        -DQT_HOST_PATH="$QT_HOST_PATH" \
        -DOC_PLUGIN_DEST=; then
    echo
    echo "CMAKE-KONFIGURATION FEHLGESCHLAGEN"
    exit 1
fi
echo

# Step 3: Bauen
echo "============================================================"
echo "[STEP 3] Bauen"
echo "============================================================"
if ! cmake --build "$BUILD_DIR" -j "$OC_JOBS"; then
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
BRX=$(ls -1 "$BUILD_DIR"/Release/opencirt-*.brx 2>/dev/null | head -1)
echo "Ausgabe: ${BRX#$ROOT/}"
ls -l "$BRX"
echo "Kopie im Beispielprojekt: sample_project/00- BricsCAD Plugin/00- Windows Version/$(basename "$BRX")"
