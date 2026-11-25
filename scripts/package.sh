#!/usr/bin/env bash
# Prepara i pacchetti di una versione di Alleggerisci nella cartella indicata:
#   Alleggerisci-X.Y.Z-portabile.zip        cartella pronta all'uso, con portabile.ini (niente nel registro di Windows)
#   Alleggerisci-X.Y.Z-installazione.exe    installatore (Inno Setup 7)
#   Alleggerisci-X.Y.Z-sorgenti-terze-parti.zip  sorgenti delle librerie distribuite (GPL, LGPL...)
#   SHA256SUMS.txt
#
# Uso (Git Bash, nella cartella del progetto, dopo "cmake --build build"):  scripts/package.sh <cartella di uscita>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:?uso: package.sh <cartella di uscita>}"
VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
DIST="$ROOT/dist/Alleggerisci"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

rm -rf "$DIST"
cmake --install "$ROOT/build" --prefix "$DIST"
[ ! -f "$DIST/portabile.ini" ] || { echo "portabile.ini non deve stare nella cartella installata" >&2; exit 1; }

# Installatore (senza portabile.ini: le scelte vanno nelle impostazioni dell'utente).
"$ROOT/scripts/build-installer.sh" "$DIST" "$OUT"

# Sorgenti delle librerie.
rm -rf "$OUT/sorgenti"
MSYS2_ROOT="${MSYS2_ROOT-/c/msys64}" "$ROOT/scripts/collect-licenses.sh" "$DIST" --sorgenti "$OUT/sorgenti"
(cd "$OUT" && rm -f "Alleggerisci-$VERSION-sorgenti-terze-parti.zip" && /c/Windows/System32/tar.exe -a -cf "Alleggerisci-$VERSION-sorgenti-terze-parti.zip" sorgenti)

# Portabile: la stessa cartella con portabile.ini vuoto e le istruzioni.
PORT="$OUT/portabile/Alleggerisci"
rm -rf "$OUT/portabile"
mkdir -p "$PORT"
cp -r "$DIST/." "$PORT/"
: > "$PORT/portabile.ini"
cp "$ROOT/installer/LEGGIMI-portabile.txt" "$PORT/LEGGIMI.txt"
(cd "$OUT/portabile" && rm -f "../Alleggerisci-$VERSION-portabile.zip" && /c/Windows/System32/tar.exe -a -cf "../Alleggerisci-$VERSION-portabile.zip" Alleggerisci)
rm -rf "$OUT/portabile"

(cd "$OUT" && sha256sum Alleggerisci-"$VERSION"-* > SHA256SUMS.txt && cat SHA256SUMS.txt)
