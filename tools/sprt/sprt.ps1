# SPRT regresyon macI: iki motor surumunu cutechess-cli ile oynatir ve SPRT
# (Sequential Probability Ratio Test) karariyla "yeni surum bazdan daha mi guclu?"
# sorusunu Elo ile yanitlar. cutechess SPRT sinirina ulasinca (H0 ya da H1) macI
# otomatik durdurur.
#
# ONEMLI (CLAUDE.md): Bundan sonraki HER arama/eval degisikligi bu kapidan gecer.
# Dugum sayisi dususu "iyilesme" degildir; yalnizca bu testin H1'i (yeni >= baz+elo1)
# reddedememesi ve pozitif Elo Elo sayilir.
#
# On kosul:
#   1) cutechess-cli kurulu (bkz. tools\sprt\README.md).
#   2) Iki etiketli binary uretilmis:
#        pwsh tools\sprt\build-version.ps1 -Ref HEAD~1 -Label base
#        pwsh tools\sprt\build-version.ps1 -Ref HEAD   -Label new
#   3) Acilis kitabi: tools\sprt\book.epd (gen-book.ps1 uretir).
#
# Kullanim (proje kokunden):
#   pwsh tools\sprt\sprt.ps1 -New new -Base base
#   pwsh tools\sprt\sprt.ps1 -New new -Base base -Tc "5+0.05" -Elo0 0 -Elo1 5

param(
    [Parameter(Mandatory=$true)][string]$New,      # yeni surum etiketi
    [Parameter(Mandatory=$true)][string]$Base,     # baz surum etiketi
    [string]$Tc          = "10+0.1",               # zaman kontrolu (sn+increment)
    [int]   $Rounds      = 5000,                    # ust sinir; SPRT genelde once biter
    [int]   $Concurrency = 4,                       # paralel oyun (CPU cekirdegine gore)
    [double]$Elo0        = 0,                        # H0: yeni surum <= baz + Elo0
    [double]$Elo1        = 5,                        # H1: yeni surum >= baz + Elo1
    [double]$Alpha       = 0.05,                     # tip-I hata
    [double]$Beta        = 0.05,                     # tip-II hata
    [string]$Cutechess   = "",                       # cutechess-cli yolu (bos = ara)
    [string]$Book        = ""                        # acilis kitabi (bos = varsayilan)
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path

# --- cutechess-cli'yi bul ---
if ([string]::IsNullOrWhiteSpace($Cutechess)) {
    $cc = Get-Command cutechess-cli -ErrorAction SilentlyContinue
    if ($cc) {
        $Cutechess = $cc.Source
    } else {
        $cand = @(
            "C:\Program Files\Cute Chess\cutechess-cli.exe",
            "C:\Program Files (x86)\Cute Chess\cutechess-cli.exe",
            "$env:LOCALAPPDATA\Programs\Cute Chess\cutechess-cli.exe"
        ) | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($cand) { $Cutechess = $cand }
    }
}
if ([string]::IsNullOrWhiteSpace($Cutechess) -or -not (Test-Path $Cutechess)) {
    throw "cutechess-cli bulunamadi. -Cutechess ile yol verin ya da kurun (bkz. tools\sprt\README.md)."
}

# --- Binary'ler ve kitap ---
$newExe  = Join-Path $repo "build-release\chess-$New.exe"
$baseExe = Join-Path $repo "build-release\chess-$Base.exe"
if ([string]::IsNullOrWhiteSpace($Book)) { $Book = Join-Path $repo "tools\sprt\book.epd" }
foreach ($p in @($newExe, $baseExe, $Book)) {
    if (-not (Test-Path $p)) { throw "Bulunamadi: $p" }
}

$pgnOut = Join-Path $repo "build-release\sprt-$New-vs-$Base.pgn"

Write-Output "SPRT: '$New' vs '$Base'  tc=$Tc  H0(<=+$Elo0) H1(>=+$Elo1) alpha=$Alpha beta=$Beta"
Write-Output "cutechess: $Cutechess"
Write-Output ""

# cutechess-cli argumanlari. -repeat + -games 2: her acilis iki tarafca oynanir
# (renk yanliligi elenir). -sprt: karara varinca otomatik durur.
$ccArgs = @(
    "-engine", "name=$New",  "cmd=$newExe",  "proto=uci",
    "-engine", "name=$Base", "cmd=$baseExe", "proto=uci",
    "-each", "tc=$Tc", "timemargin=200",
    "-openings", "file=$Book", "format=epd", "order=random",
    "-repeat", "-games", "2", "-rounds", "$Rounds",
    "-sprt", "elo0=$Elo0", "elo1=$Elo1", "alpha=$Alpha", "beta=$Beta",
    "-concurrency", "$Concurrency",
    "-ratinginterval", "20",
    "-pgnout", $pgnOut
)

& $Cutechess @ccArgs
Write-Output ""
Write-Output "PGN: $pgnOut"
