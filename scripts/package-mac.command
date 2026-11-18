#!/bin/bash
# MULTITAILS - pacchetti macOS per la Release, compilati su questo Mac.
# Fa gli stessi passaggi del job macOS della CI: build universale (Apple Silicon + Intel, da macOS 10.13),
# test, firma ad hoc, controllo della versione minima, auval, pluginval, zip e installer .pkg.
# Doppio clic dal Finder. I file finiscono in dist-mac/, il log in package_log.txt.
# Alla fine crea PACKAGE_OK (o PACKAGE_FAILED) nella cartella del progetto.
cd "$(dirname "$0")/.."
rm -f PACKAGE_OK PACKAGE_FAILED
exec > >(tee package_log.txt) 2>&1
fail() { echo ""; echo "!! ERRORE: $1"; touch PACKAGE_FAILED; echo "(puoi chiudere questa finestra)"; exit 1; }

VERSION=$(sed -n 's/^project(Multitails VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
TAG="v$VERSION"
echo "=== MULTITAILS $TAG - pacchetti macOS - $(date)"
echo "macOS $(sw_vers -productVersion), $(uname -m)"

xcode-select -p >/dev/null 2>&1 || fail "mancano i Command Line Tools: lancia prima scripts/build-mac.command"
CMAKE=$(command -v cmake || true)
[ -z "$CMAKE" ] && [ -x "$(/usr/bin/python3 -m site --user-base 2>/dev/null)/bin/cmake" ] && CMAKE="$(/usr/bin/python3 -m site --user-base)/bin/cmake"
[ -x "$CMAKE" ] || fail "CMake non trovato: lancia prima scripts/build-mac.command"

# 1) build universale, come in CI (il default di CMakeLists.txt è arm64;x86_64, da 10.13)
B=build-release
echo ">> Configuro (universale arm64 + x86_64)..."
"$CMAKE" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DMULTITAILS_BUILD_AAX=ON -DMULTITAILS_BUILD_TESTS=ON -DMULTITAILS_AAX_COPY_DIR="$PWD/$B/aax-installed" || fail "configurazione"
echo ">> Compilo (alcuni minuti)..."
"$CMAKE" --build "$B" --config Release -j "$(sysctl -n hw.ncpu)" || fail "compilazione"
A="$B/Multitails_artefacts/Release"

# 2) test
echo ">> Test..."
T=$(find "$B/Tests" -name MultitailsTests -type f -perm -u+x | head -1)
[ -n "$T" ] || fail "eseguibile dei test non trovato"
"$T" || fail "test"

# 3) firma ad hoc (anche le copie installate nella cartella utente)
echo ">> Firma ad hoc..."
for b in "$A/VST3/MULTITAILS.vst3" "$A/AU/MULTITAILS.component" \
         ~/Library/Audio/Plug-Ins/VST3/"MULTITAILS.vst3" ~/Library/Audio/Plug-Ins/Components/"MULTITAILS.component"; do
  codesign --force --deep --sign - "$b" || fail "firma di $b"
  codesign --verify --deep --strict "$b" || fail "verifica firma di $b"
done

# 4) versione minima: Intel 10.13, Apple Silicon 11.0
echo ">> Versione minima di macOS..."
for b in "$A/VST3/MULTITAILS.vst3" "$A/AU/MULTITAILS.component"; do
  bin="$b/Contents/MacOS/MULTITAILS"
  x86=$(vtool -arch x86_64 -show-build "$bin" | awk '/LC_VERSION_MIN_MACOSX/{m=1} m && $1=="version"{print $2; exit} $1=="minos"{print $2; exit}')
  arm=$(vtool -arch arm64 -show-build "$bin" | awk '$1=="minos"{print $2; exit}')
  echo "   $(basename "$b"): x86_64 $x86, arm64 $arm"
  [ "$x86" = "10.13" ] && [ "$arm" = "11.0" ] || fail "versione minima sbagliata in $b"
done

# 5) auval
echo ">> auval..."
killall -9 AudioComponentRegistrar >/dev/null 2>&1; sleep 2
auval -v aumf Mtls Gsof > auval_release_log.txt 2>&1
tail -3 auval_release_log.txt
grep -q "AU VALIDATION SUCCEEDED" auval_release_log.txt || fail "auval"

# 6) pluginval, se si riesce a scaricarlo
echo ">> pluginval (livello 8)..."
PV="$B/pluginval"
mkdir -p "$PV"
if curl -sSL -o "$PV/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip && (cd "$PV" && unzip -qo pluginval.zip); then
  "$PV/pluginval.app/Contents/MacOS/pluginval" --strictness-level 8 --skip-gui-tests --validate-in-process --timeout-ms 900000 \
    ~/Library/Audio/Plug-Ins/VST3/"MULTITAILS.vst3" > pluginval_release_log.txt 2>&1 || { tail -20 pluginval_release_log.txt; fail "pluginval"; }
  tail -2 pluginval_release_log.txt
else
  echo "   pluginval non scaricabile: salto questo controllo"
fi

# 7) pacchetti, con gli stessi nomi della CI
echo ">> Pacchetti..."
D=dist-mac
mkdir -p "$D"
rm -f "$D"/Multitails-*-macOS-*
ditto -c -k --keepParent "$A/VST3/MULTITAILS.vst3" "$D/Multitails-$TAG-macOS-VST3.zip"
ditto -c -k --keepParent "$A/AU/MULTITAILS.component" "$D/Multitails-$TAG-macOS-AU.zip"
[ -d "$A/AAX/MULTITAILS.aaxplugin" ] && ditto -c -k --keepParent "$A/AAX/MULTITAILS.aaxplugin" "$D/Multitails-$TAG-macOS-AAX-unsigned.zip"
R="$B/pkgroot"
mkdir -p "$R/Library/Audio/Plug-Ins/VST3" "$R/Library/Audio/Plug-Ins/Components"
ditto "$A/VST3/MULTITAILS.vst3" "$R/Library/Audio/Plug-Ins/VST3/MULTITAILS.vst3"
ditto "$A/AU/MULTITAILS.component" "$R/Library/Audio/Plug-Ins/Components/MULTITAILS.component"
pkgbuild --root "$R" --component-plist scripts/pkg-components.plist --identifier com.gianlucasofia.multitails.pkg \
  --version "$VERSION" --install-location / "$D/Multitails-$TAG-macOS-Installer.pkg" || fail "installer"
pkgutil --payload-files "$D/Multitails-$TAG-macOS-Installer.pkg" | grep -q "MULTITAILS.component/Contents/MacOS/MULTITAILS" || fail "installer incompleto"

killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
echo ""
echo "=== FATTO. Pacchetti in $D:"
ls -la "$D"
touch PACKAGE_OK
echo "(puoi chiudere questa finestra)"
