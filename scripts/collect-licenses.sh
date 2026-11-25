#!/usr/bin/env bash
# Licenze dei componenti distribuiti con Alleggerisci.
#
# Per ogni DLL ed eseguibile della cartella di distribuzione cerca il pacchetto MSYS2 da cui viene, ne copia
# i testi di licenza in <cartella>/licenze/<pacchetto>/ e scrive <cartella>/LICENZE-TERZE-PARTI.txt con
# versione, licenza (SPDX), sito e indirizzo esatto dei sorgenti di quella versione. Lo esegue "cmake --install";
# a mano:
#
#   scripts/collect-licenses.sh <cartella di distribuzione> [--sorgenti <cartella>]
#
# Con --sorgenti scarica anche i pacchetti sorgente (GPL, LGPL, MPL...) nella cartella indicata, da
# allegare a una release: così i sorgenti restano disponibili anche se MSYS2 li togliesse dal suo archivio.
set -euo pipefail

DEST="${1:?uso: collect-licenses.sh <cartella di distribuzione> [--sorgenti <cartella>]}"
SOURCES=""
if [ "${2:-}" = "--sorgenti" ]; then
    SOURCES="${3:?manca la cartella per i sorgenti}"
fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MSYS="${MSYS2_ROOT-/c/msys64}"   # vuoto dentro la bash di MSYS2 (la radice è /)
if [ -z "$MSYS" ]; then export PATH="/usr/bin:$PATH"; fi
PACMAN="$MSYS/usr/bin/pacman.exe"
DB="$MSYS/var/lib/pacman/local"
LICDIR="$MSYS/ucrt64/share/licenses"
TEXTS="$LICDIR/qt6-base"   # testi SPDX standard (GPL, LGPL, MIT, Apache, MPL...) distribuiti con Qt
OUT="$DEST/LICENZE-TERZE-PARTI.txt"

if [ ! -x "$PACMAN" ]; then
    echo "collect-licenses: pacman non trovato in $MSYS: licenze di terze parti non raccolte" >&2
    exit 1
fi

rm -rf "$DEST/licenze"
mkdir -p "$DEST/licenze/testi"
cp "$ROOT/LICENSE" "$DEST/LICENSE.txt"
# Il programma usa libimagequant (GPL-3.0-or-later o licenza commerciale): qui vale la GPL, come per Alleggerisci.

# Pacchetti delle DLL (Alleggerisci.exe è il programma stesso).
declare -A seen=()
declare -A files_of=()
packages=()
while IFS= read -r -d '' f; do
    rel="${f#"$DEST"/}"
    case "$rel" in
        Alleggerisci.exe) continue ;;
    esac
    name="$(basename "$f")"
    owner=""
    for candidate in "/ucrt64/bin/$name" "/ucrt64/share/qt6/plugins/$rel"; do
        owner="$("$PACMAN" -Qqo "$candidate" 2>/dev/null || true)"
        [ -n "$owner" ] && break
    done
    if [ -z "$owner" ]; then
        echo "collect-licenses: pacchetto di $rel non trovato" >&2
        continue
    fi
    if [ -z "${seen[$owner]:-}" ]; then
        seen[$owner]=1
        packages+=("$owner")
    fi
    files_of[$owner]="${files_of[$owner]:-}$rel "
done < <(find "$DEST" -type f \( -iname '*.dll' -o -iname '*.exe' \) -print0)

spdx_texts() {   # copia i testi standard delle licenze SPDX citate
    local ids
    ids="$(echo "$1" | sed -e 's/spdx://g' -e 's/[()]/ /g' | tr ' ' '\n' | grep -v -E '^(AND|OR|WITH|)$' || true)"
    for id in $ids; do
        [ -f "$TEXTS/$id.txt" ] && cp -n "$TEXTS/$id.txt" "$DEST/licenze/testi/" || true
    done
}

{
    echo "Alleggerisci - componenti di terze parti"
    echo "========================================="
    echo
    echo "Alleggerisci è software libero distribuito secondo la GNU General Public License v3.0 o successiva"
    echo "(LICENSE.txt), senza alcuna garanzia. I sorgenti sono in https://github.com/SamuDiPi/TinyImage (la"
    echo "versione di questa copia è indicata in Informazioni)."
    echo
    echo "Con Alleggerisci sono distribuiti i componenti seguenti, ciascuno con la sua licenza. I testi delle"
    echo "licenze sono nella cartella \"licenze\": per pacchetto (avvisi di copyright originali) e, in"
    echo "\"licenze/testi\", i testi standard indicati dai codici SPDX. Le librerie sono collegate come DLL"
    echo "separate e possono essere sostituite con altre versioni compatibili (LGPL)."
    echo
    echo "I sorgenti corrispondenti a ciascun componente sono all'indirizzo indicato. Su richiesta (una"
    echo "segnalazione su https://github.com/SamuDiPi/TinyImage/issues) li forniamo anche direttamente, per"
    echo "almeno tre anni dalla distribuzione di questa versione."
    echo
    echo "------------------------------------------------------------------------------------------------"
    for pkg in $(printf '%s\n' "${packages[@]}" | sort); do
        info="$("$PACMAN" -Qi "$pkg")"
        version="$(echo "$info" | sed -n 's/^Version *: //p')"
        url="$(echo "$info" | sed -n 's/^URL *: //p')"
        licenses="$(echo "$info" | sed -n 's/^Licenses *: //p')"
        base="$(sed -n '/^%BASE%$/{n;p;}' "$DB/$pkg-$version/desc")"
        short="${pkg#mingw-w64-ucrt-x86_64-}"
        files="${files_of[$pkg]}"
        echo
        echo "$short $version"
        echo "  File:      $files"
        echo "  Licenza:   ${licenses#spdx:}"
        echo "  Sito:      $url"
        if [[ "$version" == *-*.* ]]; then
            # Pacchetto ricompilato da noi (revisione con il punto, es. 4.1.0-1.1): ricetta MSYS2 con correzioni di
            # sicurezza ufficiali del progetto, in attesa della versione ufficiale. I sorgenti sono nell'archivio.
            echo "  Sorgenti:  $base-$version.src.tar.zst nell'archivio dei sorgenti di terze parti di questa versione"
            echo "             (ricetta MSYS2 ricompilata con le correzioni di sicurezza pubblicate dal progetto)"
        else
            echo "  Sorgenti:  https://repo.msys2.org/mingw/sources/$base-$version.src.tar.zst"
            echo "             (ricetta di compilazione: https://github.com/msys2/MINGW-packages/tree/master/${base#mingw-w64-})"
        fi
        # Avvisi di licenza e copyright del pacchetto (share/licenses/<nome>, LICENSE di ICU...).
        while IFS= read -r lf; do
            [ -f "$MSYS$lf" ] || continue
            mkdir -p "$DEST/licenze/$short"
            cp "$MSYS$lf" "$DEST/licenze/$short/"
        done < <("$PACMAN" -Qlq "$pkg" | grep -i -E '/share/licenses/.+[^/]$|/(licen[cs]e|copying|notice)[^/]*$' || true)
        spdx_texts "$licenses"
        if [ -n "$SOURCES" ]; then
            mkdir -p "$SOURCES"
            tarball="$base-$version.src.tar.zst"
            [ -f "$SOURCES/$tarball" ] || curl -fsSL -o "$SOURCES/$tarball" "https://repo.msys2.org/mingw/sources/$tarball"
        fi
    done

    echo
    echo "Inno Setup (solo nell'installatore Alleggerisci-X.Y.Z-installazione.exe, non nel programma)"
    echo "  Copyright (C) 1997-2026 Jordan Russell. Portions Copyright (C) 2000-2026 Martijn Laan."
    echo "  Licenza:   licenza di Inno Setup (testo in licenze/inno-setup/license.txt)"
    echo "  Sito:      https://jrsoftware.org/isinfo.php"
    mkdir -p "$DEST/licenze/inno-setup"
    cp "$ROOT/installer/inno-setup-license.txt" "$DEST/licenze/inno-setup/license.txt"

    echo
    echo "------------------------------------------------------------------------------------------------"
    echo "Marchi: Alleggerisci non è collegato ad altri programmi o servizi di compressione di immagini. JPEG e PNG"
    echo "indicano solo il formato dei file; i marchi appartengono ai rispettivi titolari."
} > "$OUT"

echo "Licenze di terze parti: ${#packages[@]} pacchetti, elenco in $OUT"
