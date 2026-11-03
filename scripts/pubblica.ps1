# MULTITAILS - pubblica le modifiche locali e il tag di una versione (default v0.1.0).
# Doppio clic su pubblica.bat. Da PowerShell:  scripts\pubblica.ps1  oppure  scripts\pubblica.ps1 -Version 0.1.1
#
# Cosa fa:
#  0. se la cartella non ha ancora .git ma contiene multitails.bundle, ricrea il repository dal bundle;
#  1. installa Git con winget se manca;
#  2. fa commit delle modifiche locali sul branch di lavoro (default "redesign") e lo pusha su GitHub;
#  3. porta main allo stato di GitHub e ci fa il merge del branch di lavoro;
#  4. pusha main, crea il tag vX.Y.Z e lo pusha. La CI compila e pubblica la Release.
# Si ferma senza toccare GitHub se il tag esiste gia' online o se il merge ha conflitti.

param(
    [string]$Version = "0.1.0",
    [string]$Branch  = "redesign",
    [string]$Repo    = "gianlucasofia90/multitails"
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "!! ERRORE: $msg" -ForegroundColor Red
    Read-Host "Premi Invio per chiudere" | Out-Null
    exit 1
}
function Step([string]$msg) { Write-Host ">> $msg" -ForegroundColor Cyan }
function G {
    & git @args
    if ($LASTEXITCODE -ne 0) { Fail ("git " + ($args -join " ")) }
}
function Quiet([scriptblock]$block) {
    $old = $ErrorActionPreference; $ErrorActionPreference = "SilentlyContinue"
    & $block *> $null
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old
    return $code
}

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Tag = "v$Version"
$RemoteUrl = "https://github.com/$Repo.git"

Write-Host "=== MULTITAILS: push, merge su main e tag $Tag - $(Get-Date)" -ForegroundColor Yellow
Write-Host "Cartella: $Root"
if ($Version -notmatch '^\d+\.\d+\.\d+$') { Fail "versione non valida: '$Version' (formato X.Y.Z)" }
if (-not (Test-Path "CMakeLists.txt")) { Fail "non trovo CMakeLists.txt: lo script deve stare in scripts\ del progetto" }

# ---------------------------------------------------------------- 1) Git
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail "Git non trovato e winget non disponibile: installa Git per Windows e rilancia" }
    Step "Installo Git con winget..."
    winget install --id Git.Git -e --source winget --accept-package-agreements --accept-source-agreements
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git installato ma non ancora nel PATH: chiudi questa finestra e rilancia" }
}
Step "$(git --version)"

# ---------------------------------------------------------------- 0) repository dal bundle, se serve
if (-not (Test-Path ".git")) {
    if (-not (Test-Path "multitails.bundle")) { Fail "la cartella non e' un repository git e non c'e' multitails.bundle" }
    Step "Ricreo il repository da multitails.bundle..."
    G init -b main
    G fetch multitails.bundle main
    G update-ref refs/heads/main FETCH_HEAD
    G symbolic-ref HEAD refs/heads/main
    G reset --hard
}
if (Test-Path "multitails.bundle") { Remove-Item "multitails.bundle" }
if (Test-Path "ripristina-git.bat") { Remove-Item "ripristina-git.bat" }

$origin = git remote get-url origin 2>$null
if (-not $origin) { G remote add origin $RemoteUrl; $origin = $RemoteUrl }
Step "Remote: $origin"

if (-not (git config user.name)) {
    $name = Read-Host "Nome per i commit git (es. gianlucasofia90)"
    $mail = Read-Host "Email per i commit git"
    G config --global user.name $name
    G config --global user.email $mail
}

G fetch --tags origin
if ((Quiet { git ls-remote --exit-code --tags origin "refs/tags/$Tag" }) -eq 0) { Fail "il tag $Tag esiste gia' su GitHub: scegli un'altra versione (-Version X.Y.Z)" }
if ((Quiet { git rev-parse -q --verify "refs/tags/$Tag" }) -eq 0) {
    Step "Tolgo il tag $Tag rimasto solo in locale"
    G tag -d $Tag
}

# ---------------------------------------------------------------- 2) branch di lavoro
$current = (git branch --show-current).Trim()
if (-not $current) { Fail "nessun branch attivo (HEAD staccato)" }
if ($current -ne $Branch) {
    Step "Porto il lavoro di '$current' sul branch '$Branch'"
    G checkout -B $Branch
}

Write-Host ""
Write-Host "Modifiche locali non ancora in un commit:"
git status --short
Write-Host ""
Write-Host "Commit che andranno su main:"
git log --oneline "origin/main..$Branch"
Write-Host ""
$ok = Read-Host "Procedo con push di '$Branch', merge su main e tag $Tag? [s/N]"
if ($ok -notmatch '^[sSyY]$') { Fail "annullato" }

G add -A
if ((Quiet { git diff --cached --quiet }) -ne 0) {
    $msg = Read-Host "Messaggio per il commit delle modifiche (Invio = Ritocchi all'interfaccia)"
    if (-not $msg) { $msg = "Ritocchi all'interfaccia" }
    G commit -m $msg
}

Step "Push di '$Branch'..."
G push -u origin $Branch

# ---------------------------------------------------------------- 3) merge su main
Step "Merge di '$Branch' su main..."
G checkout -B main origin/main
git merge --no-ff -m "Merge del branch '$Branch' per la versione $Version" $Branch
if ($LASTEXITCODE -ne 0) {
    git merge --abort *> $null
    G checkout $Branch
    Fail "il merge su main ha conflitti: niente e' stato pushato su main. Risolvili a mano e rilancia lo script"
}

$cm = [IO.File]::ReadAllText((Join-Path $Root "CMakeLists.txt"))
if ($cm -notmatch "(?m)^project\(Multitails VERSION $([regex]::Escape($Version)) ") {
    Write-Host "!! CMakeLists.txt non dichiara la versione $Version" -ForegroundColor Yellow
}

# ---------------------------------------------------------------- 4) push di main e tag
Step "Push di main..."
G push -u origin main
Step "Creo e pusho il tag $Tag..."
G tag -a $Tag -m "MULTITAILS $Version"
G push origin $Tag

$web = $origin -replace '^git@github\.com:', 'https://github.com/' -replace '\.git$', ''
Write-Host ""
Write-Host "=== FATTO. main aggiornato e $Tag pubblicato." -ForegroundColor Green
Write-Host "La CI sta compilando la Release: $web/actions"
Write-Host "Al termine la trovi in:          $web/releases/tag/$Tag"
Read-Host "Premi Invio per chiudere" | Out-Null
