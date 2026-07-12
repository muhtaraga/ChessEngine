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
#        powershell -File tools\sprt\build-version.ps1 -Ref HEAD~1 -Label base
#        powershell -File tools\sprt\build-version.ps1 -Ref HEAD   -Label new
#   3) Acilis kitabi: tools\sprt\book.epd (gen-book.ps1 uretir).
#
# Kullanim (proje kokunden):
#   powershell -File tools\sprt\sprt.ps1 -New new -Base base
#   powershell -File tools\sprt\sprt.ps1 -New new -Base base -Tc "5+0.05" -Elo0 0 -Elo1 5
#
# Lazy SMP olcekleme testi (ayni kod, farkli thread; new 2 thread vs base 1 thread):
#   # ayni commit'i iki etiketle derle (motor isimleri farkli olmali):
#   powershell -File tools\sprt\build-version.ps1 -Ref HEAD -Label smp1
#   powershell -File tools\sprt\build-version.ps1 -Ref HEAD -Label smp2
#   powershell -File tools\sprt\sprt.ps1 -New smp2 -Base smp1 -NewThreads 2 -BaseThreads 1 -Concurrency 6
#   (Iki binary AYNI koddur; fark yalniz thread sayisi. Elo>0 ise SMP gercekten guc
#    veriyor. concurrency*max_thread <= fiziksel cekirdek olsun -> script uyarir.)

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
    [int]   $Hash        = 0,                         # motor basina TT (MB); 0 = ayarlama
    [string]$Cutechess   = "",                       # cutechess-cli yolu (bos = ara)
    [string]$Book        = "",                        # acilis kitabi (bos = varsayilan)
    [string]$NewEvalFile = "",                        # yeni motora option.EvalFile (Texel tuning)
    [string]$BaseEvalFile = "",                       # baz motora option.EvalFile (genelde bos)
    [int]   $NewThreads  = 0,                          # yeni motora option.Threads (Lazy SMP); 0 = ayarlama (=1)
    [int]   $BaseThreads = 0                           # baz motora option.Threads; 0 = ayarlama (=1)
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

$hashInfo = if ($Hash -gt 0) { " hash=${Hash}MB" } else { "" }
Write-Output "SPRT: '$New' vs '$Base'  tc=$Tc$hashInfo  H0(<=+$Elo0) H1(>=+$Elo1) alpha=$Alpha beta=$Beta"
Write-Output "cutechess: $Cutechess"
Write-Output ""

# cutechess-cli argumanlari. -repeat + -games 2: her acilis iki tarafca oynanir
# (renk yanliligi elenir). -sprt: karara varinca otomatik durur.
# -each altina konan ayarlar iki motora da esit uygulanir; Hash>0 ise ayni TT boyutu.
$eachArgs = @("tc=$Tc", "timemargin=200")
if ($Hash -gt 0) { $eachArgs += "option.Hash=$Hash" }

# Motor basina EvalFile (Texel tuning): yalniz ilgili motora option.EvalFile eklenir.
# Yol mutlaklastirilir (cutechess calisma dizini farkli olabilir).
$newEngine  = @("-engine", "name=$New",  "cmd=$newExe",  "proto=uci")
if (-not [string]::IsNullOrWhiteSpace($NewEvalFile)) {
    $newEngine += "option.EvalFile=" + (Resolve-Path $NewEvalFile).Path
}
if ($NewThreads -gt 0)  { $newEngine  += "option.Threads=$NewThreads" }
$baseEngine = @("-engine", "name=$Base", "cmd=$baseExe", "proto=uci")
if (-not [string]::IsNullOrWhiteSpace($BaseEvalFile)) {
    $baseEngine += "option.EvalFile=" + (Resolve-Path $BaseEvalFile).Path
}
if ($BaseThreads -gt 0) { $baseEngine += "option.Threads=$BaseThreads" }

# Lazy SMP olcekleme testi: motorlar farkli thread sayisiyla kosuyorsa (orn. base=1
# vs new=2) CPU'yu asiri-abone etme uyarisi. Bir oyunda ayni anda tek taraf arar,
# yani oyun basi tepe yuk ~= max(new,base) thread; concurrency * bu <= fiziksel
# cekirdek olmali ki thread'ler gercekten ayri cekirdekte kossun (yoksa Elo yaniltir).
$maxThreads = [Math]::Max([Math]::Max($NewThreads,1), [Math]::Max($BaseThreads,1))
if ($maxThreads -gt 1) {
    $physCores = try {
        (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum
    } catch { [int]$env:NUMBER_OF_PROCESSORS }
    $load = $Concurrency * $maxThreads
    Write-Output "SMP olcekleme: new=$([Math]::Max($NewThreads,1))t base=$([Math]::Max($BaseThreads,1))t  concurrency=$Concurrency  tepe-yuk~$load vs fiziksel cekirdek=$physCores"
    if ($load -gt $physCores) {
        Write-Output "UYARI: concurrency*max_thread ($load) > fiziksel cekirdek ($physCores) -> thread'ler cekirdek paylasir, Elo yaniltici olabilir. -Concurrency dusurun (oneri: <= $([Math]::Floor($physCores / $maxThreads)))."
    }
    Write-Output ""
}

$ccArgs = $newEngine + $baseEngine + @("-each") + $eachArgs + @(
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
