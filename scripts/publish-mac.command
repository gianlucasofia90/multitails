#!/bin/bash
# MULTITAILS - pubblica la versione di CMakeLists.txt su GitHub dal Mac, senza domande.
# Da lanciare dopo scripts/package-mac.command (pacchetti macOS in dist-mac/).
# - con GitHub CLI (gh) autenticato: crea la Release vX.Y.Z con le note del CHANGELOG e i pacchetti macOS;
#   il tag nuovo fa partire la CI, che aggiunge Windows e Linux alla stessa Release.
# - senza gh: fa solo il push del tag; la CI crea la Release con Windows e Linux e i pacchetti macOS
#   restano da allegare a mano dalla pagina della Release.
# Doppio clic dal Finder. Log in publish_log.txt; alla fine PUBLISH_OK o PUBLISH_FAILED.
cd "$(dirname "$0")/.." || exit 1
rm -f PUBLISH_OK PUBLISH_FAILED
exec > >(tee publish_log.txt) 2>&1
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export GIT_TERMINAL_PROMPT=0            # niente richieste di password: se mancano le credenziali, errore subito
fail() { echo ""; echo "!! ERRORE: $1"; touch PUBLISH_FAILED; echo "(puoi chiudere questa finestra)"; exit 1; }

VERSION=$(sed -n 's/^project(Multitails VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
TAG="v$VERSION"
echo "=== MULTITAILS pubblicazione $TAG - $(date)"

# 1) controlli
[ -n "$VERSION" ] || fail "versione non trovata in CMakeLists.txt"
grep -q "^## \[$VERSION\]" CHANGELOG.md || fail "manca la voce [$VERSION] nel CHANGELOG"
[ "$(git branch --show-current)" = "main" ] || fail "non sei su main"
[ -z "$(git status --porcelain --untracked-files=no)" ] || fail "ci sono modifiche non committate"
git fetch --tags origin || fail "impossibile contattare GitHub (rete o credenziali)"
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || fail "main locale diverso da GitHub: allinealo prima"
git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1 && fail "il tag $TAG esiste già su GitHub"
shopt -s nullglob
PKGS=(dist-mac/Multitails-$TAG-macOS-*)
[ ${#PKGS[@]} -ge 3 ] || fail "pacchetti macOS di $TAG non trovati in dist-mac: lancia prima scripts/package-mac.command"
echo ">> Commit: $(git log --oneline -1)"
echo ">> Pacchetti macOS:"; printf '   %s\n' "${PKGS[@]}"

awk -v v="$VERSION" '$0 ~ "^## \\[?"v {f=1; next} /^## /{if(f) exit} f' CHANGELOG.md > notes.md
printf '\nI pacchetti macOS (universali Apple Silicon + Intel, da macOS 10.13) sono compilati su un Mac con `scripts/package-mac.command` e verificati con test, auval e pluginval; Windows e Linux li compila la CI.\n' >> notes.md

URL="$(git remote get-url origin | sed -e 's#^git@github.com:#https://github.com/#' -e 's#\.git$##')"

# 2) con gh: Release completa dei pacchetti macOS (crea anche il tag su main)
if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
  echo ">> GitHub CLI pronto: creo la Release $TAG con i pacchetti macOS"
  gh release create "$TAG" "${PKGS[@]}" --target "$(git rev-parse HEAD)" --title "MULTITAILS $TAG" --notes-file notes.md \
    || fail "creazione della Release"
  MODE="release"
else
  # 3) senza gh: solo il tag, la CI crea la Release con Windows e Linux
  echo ">> GitHub CLI non disponibile: faccio solo il push del tag"
  git tag -a "$TAG" -m "MULTITAILS $VERSION" || fail "creazione del tag"
  git push origin "$TAG" || { git tag -d "$TAG" >/dev/null; fail "push del tag (credenziali GitHub non configurate su questo Mac)"; }
  MODE="tag"
fi
rm -f notes.md

echo ""
echo "=== FATTO ($MODE). $TAG pubblicato."
echo "La CI aggiunge Windows e Linux: $URL/actions"
echo "Release: $URL/releases/tag/$TAG"
[ "$MODE" = "tag" ] && echo "I pacchetti macOS in dist-mac/ vanno allegati alla Release dalla pagina web (Edit, poi trascinali)."
echo "$MODE" > PUBLISH_OK
echo "(puoi chiudere questa finestra)"
