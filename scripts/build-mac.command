#!/bin/bash
# MULTITAILS - installa i requisiti e compila VST3 + AU + AAX sul Mac.
# Doppio clic dal Finder. Il log va in build_log.txt nella cartella del progetto.
cd "$(dirname "$0")/.."
rm -f BUILD_OK BUILD_FAILED
exec > >(tee build_log.txt) 2>&1
fail() { echo ""; echo "!! ERRORE: $1"; touch BUILD_FAILED; echo "(puoi chiudere questa finestra)"; exit 1; }

echo "=== MULTITAILS build - $(date)"
echo "macOS $(sw_vers -productVersion), $(uname -m)"

# 1) Xcode Command Line Tools
if ! xcode-select -p >/dev/null 2>&1 || ! /usr/bin/xcrun --find clang >/dev/null 2>&1; then
  echo ">> Installazione Xcode Command Line Tools: conferma 'Installa' nella finestra di macOS..."
  xcode-select --install >/dev/null 2>&1 || true
  for i in $(seq 1 360); do
    if xcode-select -p >/dev/null 2>&1 && /usr/bin/xcrun --find clang >/dev/null 2>&1; then break; fi
    sleep 10
  done
  xcode-select -p >/dev/null 2>&1 || fail "Command Line Tools non installati"
fi
echo ">> Command Line Tools: $(xcode-select -p)"

# 2) CMake (Homebrew se c'è, altrimenti pip --user)
CMAKE=$(command -v cmake || true)
[ -z "$CMAKE" ] && [ -x "$(/usr/bin/python3 -m site --user-base 2>/dev/null)/bin/cmake" ] && CMAKE="$(/usr/bin/python3 -m site --user-base)/bin/cmake"
if [ -z "$CMAKE" ] && command -v brew >/dev/null 2>&1; then
  echo ">> Installo CMake con Homebrew..."; brew install cmake && CMAKE=$(command -v cmake)
fi
if [ -z "$CMAKE" ]; then
  echo ">> Installo CMake con pip (solo per questo utente)..."
  /usr/bin/python3 -m pip install --user --upgrade cmake || fail "installazione CMake"
  CMAKE="$(/usr/bin/python3 -m site --user-base)/bin/cmake"
fi
[ -x "$CMAKE" ] || fail "CMake non trovato"
echo ">> CMake: $("$CMAKE" --version | head -1)"

# 3) build (JUCE viene scaricato in automatico al primo avvio)
ARCH=$(uname -m)
# AAX: cartella di sistema di Pro Tools se scrivibile, altrimenti resta in build-mac
AAXDIR="/Library/Application Support/Avid/Audio/Plug-Ins"
[ -w "$AAXDIR" ] || AAXDIR="$PWD/build-mac/aax-installed"
echo ">> Configuro (architettura $ARCH)..."
"$CMAKE" -B build-mac -DCMAKE_BUILD_TYPE=Release -DMULTITAILS_BUILD_AAX=ON -DMULTITAILS_AAX_COPY_DIR="$AAXDIR" -DCMAKE_OSX_ARCHITECTURES="$ARCH" || fail "configurazione"
echo ">> Compilo (qualche minuto)..."
"$CMAKE" --build build-mac --config Release -j "$(sysctl -n hw.ncpu)" || fail "compilazione"

killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
echo ""
echo "=== FATTO. Plugin installati:"
ls -d ~/Library/Audio/Plug-Ins/VST3/"MULTITAILS.vst3" ~/Library/Audio/Plug-Ins/Components/"MULTITAILS.component" 2>/dev/null
ls -d build-mac/Multitails_artefacts/Release/AAX/*.aaxplugin 2>/dev/null && echo "(AAX: da firmare con PACE per Pro Tools)"
touch BUILD_OK
echo "(puoi chiudere questa finestra)"
