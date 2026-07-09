# Zaman-katlama (time-doubling) teshisi: AYNI binary kendine karsi oynar, tek fark
# taraflarin zaman kontrolu. Olculen Elo farki = "arama suresini ikiye katlamanin degeri".
#
# NEDEN: Arama tekniklerimizin (LMR, futility, ...) getirisi dustukce sorulan soru:
# "eval mi tavan oluyor?" Bu testin cevabi dogrudan:
#   - Katlama kazanci yuksek (~+50-70 Elo) -> derinlik hala karsiligini veriyor,
#     search verimli, eval tavan DEGIL.
#   - Katlama kazanci belirgin dusuk (~+30 ve alti) -> motor daha derine iniyor ama
#     derinlik ona bir sey kazandirmiyor; cunku daha derinde de ayni kor eval'e bakiyor.
#     Eval tavaninin OLCULMUS kaniti.
# Referans bandi sert bir yasa degil, sektor deneyimi; katlama kazanci motor guclendikce
# ve TC uzadikca azalir.
#
# sprt.ps1'DEN FARKI:
#   1) Asimetrik TC: tc= her -engine blogunun ICINDE (sprt.ps1'de -each altinda, yani ortak).
#   2) SPRT YOK: karar degil KESTIRIM ariyoruz; sabit sayida oyun kosulur, cutechess'in
#      bastigi Elo +- hata okunur.
# sprt.ps1 KAPI betigidir, degistirilmedi.
#
# On kosul: cutechess-cli kurulu + tek bir etiketli binary:
#   powershell -File tools\sprt\build-version.ps1 -Ref 420d22d -Label tcdiag
#
# Kullanim (proje kokunden):
#   powershell -File tools\sprt\tc-scaling.ps1 -Label tcdiag `
#       -TcFast "5+0.05" -TcSlow "10+0.1" -Rounds 1000 -Concurrency 14 -Hash 16
#
# SAGLIK KONTROLU: 2x zaman alan taraf DAIMA belirgin kazanmali. Elo ~ 0 cikiyorsa
# once bu betikten suphelen (asimetrik tc gecmemis olabilir), motordan degil.

param(
    [Parameter(Mandatory=$true)][string]$Label,     # tek surum etiketi; iki taraf da ayni exe
    [string]$TcFast     = "5+0.05",                 # hizli taraf
    [string]$TcSlow     = "10+0.1",                 # yavas taraf (2x zaman)
    [int]   $Rounds     = 1000,                     # oyun = Rounds * 2 (-repeat)
    [int]   $Concurrency = 14,                      # fiziksel cekirdek; ASMA (zaman olcumu bozulur)
    [int]   $Hash       = 16,                       # motor basina TT (MB); 0 = ayarlama
    [string]$Cutechess  = "",                       # cutechess-cli yolu (bos = ara)
    [string]$Book       = ""                        # acilis kitabi (bos = varsayilan)
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path

# --- cutechess-cli'yi bul (sprt.ps1 ile ayni mantik) ---
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

# --- Binary ve kitap. Iki -engine girdisi de AYNI exe'yi gosterir. ---
$exe = Join-Path $repo "build-release\chess-$Label.exe"
if ([string]::IsNullOrWhiteSpace($Book)) { $Book = Join-Path $repo "tools\sprt\book.epd" }
foreach ($p in @($exe, $Book)) {
    if (-not (Test-Path $p)) { throw "Bulunamadi: $p" }
}

$pgnOut = Join-Path $repo "build-release\tcscale-$Label.pgn"

$hashInfo = if ($Hash -gt 0) { " hash=${Hash}MB" } else { "" }
Write-Output "Zaman-katlama teshisi: '$Label' kendine karsi"
Write-Output "  slow tc=$TcSlow   vs   fast tc=$TcFast$hashInfo   (oyun: $($Rounds * 2))"
Write-Output "  Beklenti: 'slow' belirgin kazanir. Elo ~ 0 ise betikten suphelen."
Write-Output "cutechess: $Cutechess"
Write-Output ""

# tc= HER -engine blogunun icinde: cutechess motor-basina ayari engine blogunda alir.
# -each altina yalnizca iki tarafa ESIT uygulanmasi gerekenler (timemargin, Hash) konur.
# Elo farki "slow - fast" olarak raporlanir (ilk -engine referanstir).
$slowArgs = @("-engine", "name=slow", "cmd=$exe", "proto=uci", "tc=$TcSlow")
$fastArgs = @("-engine", "name=fast", "cmd=$exe", "proto=uci", "tc=$TcFast")

$eachArgs = @("timemargin=200")
if ($Hash -gt 0) { $eachArgs += "option.Hash=$Hash" }

$ccArgs = $slowArgs + $fastArgs + @("-each") + $eachArgs + @(
    "-openings", "file=$Book", "format=epd", "order=random",
    "-repeat", "-games", "2", "-rounds", "$Rounds",
    "-concurrency", "$Concurrency",
    "-ratinginterval", "20",
    "-pgnout", $pgnOut
)

& $Cutechess @ccArgs
Write-Output ""
Write-Output "PGN: $pgnOut"
