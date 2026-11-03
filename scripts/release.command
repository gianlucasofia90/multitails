#!/bin/bash
# MULTITAILS - pubblica una versione su GitHub.
# Doppio clic dal Finder (va messo in scripts/), oppure da Terminale:
#   scripts/release.command            -> versione 0.1.1
#   scripts/release.command 0.1.2      -> un'altra versione
# Aggiorna la versione in CMakeLists.txt, aggiunge la voce in CHANGELOG.md se manca,
# fa commit, push del branch e crea/pusha il tag vX.Y.Z. La CI pubblica la Release.
cd "$(dirname "$0")/.." || exit 1
VERSION="${1:-0.1.1}"
TAG="v$VERSION"
TODAY="$(date +%Y-%m-%d)"

fail() { echo ""; echo "!! ERRORE: $1"; echo "(puoi chiudere questa finestra)"; exit 1; }

echo "=== MULTITAILS release $TAG - $(date)"

# 1) controlli
command -v git >/dev/null 2>&1 || fail "git non trovato (installa gli Xcode Command Line Tools)"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "questa cartella non è un repository git"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "versione non valida: '$VERSION' (formato X.Y.Z)"
git remote get-url origin >/dev/null 2>&1 || fail "manca il remote 'origin'"
BRANCH="$(git branch --show-current)"
[ -n "$BRANCH" ] || fail "nessun branch attivo (HEAD staccato)"
echo ">> Remote: $(git remote get-url origin)"
echo ">> Branch: $BRANCH"

git fetch --tags origin || fail "impossibile contattare GitHub (rete o credenziali)"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && fail "il tag $TAG esiste già in locale"
git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1 && fail "il tag $TAG esiste già su GitHub"

# il branch locale non deve essere indietro rispetto a GitHub
if git rev-parse -q --verify "origin/$BRANCH" >/dev/null; then
  BEHIND="$(git rev-list --count "HEAD..origin/$BRANCH")"
  [ "$BEHIND" = "0" ] || fail "il branch è indietro di $BEHIND commit rispetto a origin/$BRANCH: fai prima git pull"
fi

# 2) versione in CMakeLists.txt
OLD="$(sed -n 's/^project(Multitails VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$OLD" ] || fail "non trovo 'project(Multitails VERSION ...)' in CMakeLists.txt"
echo ">> Versione: $OLD -> $VERSION"

# 3) voce nel CHANGELOG, se manca: commit dall'ultimo tag
ADD_CHANGELOG=0
grep -q "^## \[$VERSION\]" CHANGELOG.md || ADD_CHANGELOG=1
if [ "$ADD_CHANGELOG" = "1" ]; then
  LAST="$(git describe --tags --abbrev=0 2>/dev/null || true)"
  if [ -n "$LAST" ]; then RANGE="$LAST..HEAD"; else RANGE="HEAD"; fi
  NOTES="$(git log --no-merges --pretty='- %s' $RANGE | grep -v -i '^- Versione [0-9]' )"
  [ -n "$NOTES" ] || NOTES="- Nuova interfaccia (tema Egg Lab), allineata a SONIC GRAIN."
  echo ">> CHANGELOG: aggiungo la voce [$VERSION] - $TODAY"
  echo "$NOTES"
fi

echo ""
read -r -p "Procedo con commit, push e tag $TAG? [s/N] " OK
[[ "$OK" =~ ^[sSyY]$ ]] || fail "annullato"

perl -pi -e "s/^project\(Multitails VERSION [0-9.]+/project(Multitails VERSION $VERSION/" CMakeLists.txt \
  || fail "aggiornamento di CMakeLists.txt"

if [ "$ADD_CHANGELOG" = "1" ]; then
  {
    sed -n '1,2p' CHANGELOG.md
    echo "## [$VERSION] - $TODAY"
    echo "$NOTES"
    echo ""
    sed -n '3,$p' CHANGELOG.md
  } > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md || fail "aggiornamento di CHANGELOG.md"
fi

# 4) commit e push
git add -A
if git diff --cached --quiet; then
  echo ">> Niente da committare"
else
  git commit -m "Versione $VERSION" || fail "commit"
fi
git push origin "$BRANCH" || fail "push del branch (controlla le credenziali: 'gh auth login' o una chiave SSH)"

# 5) tag
git tag -a "$TAG" -m "MULTITAILS $VERSION" || fail "creazione del tag"
git push origin "$TAG" || fail "push del tag"

URL="$(git remote get-url origin | sed -e 's#^git@github.com:#https://github.com/#' -e 's#\.git$##')"
echo ""
echo "=== FATTO. $TAG pubblicato."
echo "La CI sta compilando la Release: $URL/actions"
echo "Al termine la trovi in: $URL/releases/tag/$TAG"
echo "(puoi chiudere questa finestra)"
