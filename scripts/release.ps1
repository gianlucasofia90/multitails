# MULTITAILS - pubblica una versione su GitHub da Windows.
# Va messo in scripts\ insieme a release.bat (doppio clic su release.bat).
# Da PowerShell:  scripts\release.ps1            -> versione 0.1.1
#                 scripts\release.ps1 -Version 0.1.2
#
# Cosa fa:
#  1. installa Git e GitHub CLI con winget se mancano, e fa il login a GitHub;
#  2. prepara il repository: se la cartella non e' ancora un repo git la collega a GitHub
#     (senza toccare i tuoi file), se il repo su GitHub non esiste lo crea;
#  3. fa commit delle modifiche locali e le allinea con GitHub (pull --rebase);
#  4. porta la versione in CMakeLists.txt, aggiunge la voce nel CHANGELOG se manca;
#  5. push del branch, crea e pusha il tag vX.Y.Z. La CI compila e pubblica la Release.

param(
    [string]$Version = "0.1.1",
    [string]$Repo    = "gianlucasofia90/multitails"
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = [Text.Encoding]::UTF8
$Utf8NoBom = New-Object Text.UTF8Encoding $false

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "!! ERRORE: $msg" -ForegroundColor Red
    Read-Host "Premi Invio per chiudere" | Out-Null
    exit 1
}
function Step([string]$msg) { Write-Host ">> $msg" -ForegroundColor Cyan }

# esegue git e si ferma se fallisce
function G {
    & git @args
    if ($LASTEXITCODE -ne 0) { Fail ("git " + ($args -join " ")) }
}
# esegue un comando senza output e restituisce solo il codice di uscita
function Quiet([scriptblock]$block) {
    $old = $ErrorActionPreference; $ErrorActionPreference = "SilentlyContinue"
    & $block *> $null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    return $code
}
function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Tag = "v$Version"
$Today = Get-Date -Format "yyyy-MM-dd"

Write-Host "=== MULTITAILS release $Tag - $(Get-Date)" -ForegroundColor Yellow
Write-Host "Cartella: $Root"

if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "versione non valida: '$Version' (formato X.Y.Z)" }
if (-not (Test-Path "CMakeLists.txt")) { Fail "non trovo CMakeLists.txt: lo script deve stare in scripts\ del progetto" }

# ---------------------------------------------------------------- 1) strumenti
function Ensure-Tool([string]$exe, [string]$wingetId, [string]$name) {
    if (Get-Command $exe -ErrorAction SilentlyContinue) { return }
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Fail "$name non trovato e winget non disponibile. Installalo a mano e rilancia lo script."
    }
    Step "Installo $name con winget..."
    winget install --id $wingetId -e --source winget --accept-package-agreements --accept-source-agreements
    Refresh-Path
    if (-not (Get-Command $exe -ErrorAction SilentlyContinue)) {
        Fail "$name installato ma non ancora nel PATH: chiudi questa finestra e rilancia lo script"
    }
}
Ensure-Tool "git" "Git.Git" "Git"
Ensure-Tool "gh"  "GitHub.cli" "GitHub CLI"
Step "$(git --version)"

if ((Quiet { gh auth status -h github.com }) -ne 0) {
    Step "Login a GitHub (si apre il browser)..."
    gh auth login -h github.com -p https -w
    if ($LASTEXITCODE -ne 0) { Fail "login a GitHub non riuscito" }
}
gh auth setup-git -h github.com | Out-Null   # git usa le credenziali di gh per il push

# identita' git per i commit, se manca
if (-not (git config user.name)) {
    $login = gh api user --jq .login
    $id    = gh api user --jq .id
    git config --global user.name  $login
    git config --global user.email "$id+$login@users.noreply.github.com"
    Step "Identita' git impostata: $login"
}

# ---------------------------------------------------------------- 2) repository
$RemoteUrl = "https://github.com/$Repo.git"
$repoExists = (Quiet { gh repo view $Repo }) -eq 0
if (-not $repoExists) {
    Step "Il repository $Repo non esiste su GitHub: lo creo"
    gh repo create $Repo --public --description "Multi-sequencer MIDI a 16 tracce (VST3 / AU / AAX)"
    if ($LASTEXITCODE -ne 0) { Fail "creazione del repository su GitHub" }
}

if ((Quiet { git rev-parse --is-inside-work-tree }) -ne 0) {
    Step "La cartella non e' un repository git: la collego a $RemoteUrl"
    G init -b main
    G remote add origin $RemoteUrl
    if ($repoExists) {
        G fetch origin
        if ((Quiet { git rev-parse -q --verify origin/main }) -eq 0) {
            G reset origin/main                 # i file restano come sono, diventano modifiche locali
            G branch --set-upstream-to=origin/main main
        }
    }
} else {
    $origin = git remote get-url origin 2>$null
    if (-not $origin) {
        G remote add origin $RemoteUrl
    } elseif ($origin -notmatch [regex]::Escape($Repo)) {
        Write-Host "!! origin punta a $origin e non a $Repo" -ForegroundColor Yellow
        $ans = Read-Host "Continuo su $origin? [s/N]"
        if ($ans -notmatch '^[sSyY]$') { Fail "annullato" }
    }
}

$Branch = (git branch --show-current).Trim()
if (-not $Branch) { Fail "nessun branch attivo (HEAD staccato)" }
Step "Remote: $(git remote get-url origin)  -  Branch: $Branch"

G fetch --tags origin
if ((Quiet { git rev-parse -q --verify "refs/tags/$Tag" }) -eq 0) { Fail "il tag $Tag esiste gia' in locale" }
if ((Quiet { git ls-remote --exit-code --tags origin "refs/tags/$Tag" }) -eq 0) { Fail "il tag $Tag esiste gia' su GitHub" }

# fine riga corretti per gli script Windows (il repo forza LF su tutto il resto)
$ga = if (Test-Path ".gitattributes") { [IO.File]::ReadAllText((Join-Path $Root ".gitattributes")) } else { "" }
$gaAdd = ""
if ($ga -notmatch '(?m)^\*\.bat ') { $gaAdd += "*.bat text eol=crlf`n" }
if ($ga -notmatch '(?m)^\*\.ps1 ') { $gaAdd += "*.ps1 text eol=crlf`n" }

# ---------------------------------------------------------------- 3) riepilogo
$cm = [IO.File]::ReadAllText((Join-Path $Root "CMakeLists.txt"))
$m = [regex]::Match($cm, '(?m)^project\(Multitails VERSION ([0-9.]+)')
if (-not $m.Success) { Fail "non trovo 'project(Multitails VERSION ...)' in CMakeLists.txt" }
$OldVersion = $m.Groups[1].Value

Write-Host ""
Write-Host "Modifiche locali:"
git status --short
Write-Host ""
Write-Host "Versione: $OldVersion -> $Version   Tag: $Tag"
$msg = Read-Host "Messaggio per il commit delle modifiche (Invio = 'Nuova interfaccia Egg Lab')"
if (-not $msg) { $msg = "Nuova interfaccia Egg Lab" }
$ok = Read-Host "Procedo con commit, push e tag $Tag? [s/N]"
if ($ok -notmatch '^[sSyY]$') { Fail "annullato" }

# ---------------------------------------------------------------- 4) commit e allineamento
if ($gaAdd) {
    if ($ga -and -not $ga.EndsWith("`n")) { $ga += "`n" }
    [IO.File]::WriteAllText((Join-Path $Root ".gitattributes"), $ga + $gaAdd, $Utf8NoBom)
}
G add -A
if ((Quiet { git diff --cached --quiet }) -ne 0) { G commit -m $msg } else { Step "Nessuna modifica locale da committare" }

if ((Quiet { git rev-parse -q --verify "origin/$Branch" }) -eq 0) {
    Step "Allineo con origin/$Branch..."
    git pull --rebase origin $Branch
    if ($LASTEXITCODE -ne 0) {
        Fail "conflitti durante il pull. Risolvili, poi 'git rebase --continue' e rilancia lo script (oppure 'git rebase --abort')"
    }
}

# ---------------------------------------------------------------- 5) versione e changelog
$cm = [regex]::Replace($cm, '(?m)^project\(Multitails VERSION [0-9.]+', "project(Multitails VERSION $Version")
[IO.File]::WriteAllText((Join-Path $Root "CMakeLists.txt"), $cm, $Utf8NoBom)

$cl = [IO.File]::ReadAllText((Join-Path $Root "CHANGELOG.md"))
if ($cl -notmatch "(?m)^## \[$([regex]::Escape($Version))\]") {
    $lastTag = git describe --tags --abbrev=0 2>$null
    if ($lastTag) { $range = "$lastTag..HEAD" }
    elseif ((Quiet { git rev-parse -q --verify "origin/$Branch" }) -eq 0) { $range = "origin/$Branch..HEAD" }
    else { $range = "HEAD" }
    $notes = @(git log --no-merges --pretty="- %s" $range | Where-Object { $_ -notmatch '^- Versione \d' })
    if ($notes.Count -eq 0) { $notes = @("- $msg") }
    $entry = "## [$Version] - $Today`n" + ($notes -join "`n") + "`n`n"
    $i = $cl.IndexOf("## [")
    if ($i -ge 0) { $cl = $cl.Insert($i, $entry) } else { $cl = $cl.TrimEnd() + "`n`n" + $entry }
    [IO.File]::WriteAllText((Join-Path $Root "CHANGELOG.md"), $cl, $Utf8NoBom)
    Step "CHANGELOG: aggiunta la voce [$Version]"
    $notes | ForEach-Object { Write-Host "   $_" }
}

G add CMakeLists.txt CHANGELOG.md
if ((Quiet { git diff --cached --quiet }) -ne 0) { G commit -m "Versione $Version" }

# ---------------------------------------------------------------- 6) push e tag
Step "Push di $Branch..."
G push -u origin $Branch
Step "Creo il tag $Tag..."
G tag -a $Tag -m "MULTITAILS $Version"
G push origin $Tag

$web = (git remote get-url origin) -replace '^git@github\.com:', 'https://github.com/' -replace '\.git$', ''
Write-Host ""
Write-Host "=== FATTO. $Tag pubblicato." -ForegroundColor Green
Write-Host "La CI sta compilando la Release: $web/actions"
Write-Host "Al termine la trovi in:          $web/releases/tag/$Tag"
Read-Host "Premi Invio per chiudere" | Out-Null
