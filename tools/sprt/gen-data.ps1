# Texel tuning veri seti ureticisi (self-play).
#
# Motorun "chess datagen" moduyla quiet-etiketli (FEN, oyun sonucu) veri seti
# uretir. Her isci (worker) FARKLI tohumla ayri bir surece calisir (paralel),
# kendi shard dosyasina yazar; sonda hepsi tek dosyada birlestirilir. Cikti
# satir basina "<FEN> <result>", result beyaz bakisi oyun sonucu (0.0/0.5/1.0).
# Ayni veri ileride NNUE egitimi icin de kullanilabilir (bir kez kurulur, iki
# tabana hizmet eder).
#
# Kullanim (proje kokunden):
#   powershell -File tools/sprt/gen-data.ps1 -Games 200000 -Depth 8 -Workers 12
#   powershell -File tools/sprt/gen-data.ps1 -Build          # once calisan agaci derle
#
# Not: PowerShell 5.1'de "ForEach-Object -Parallel" yok; paralellik Start-Process
# ile ayri chess.exe surecleriyle saglanir. Uzun surer -> arka planda birak.

param(
    [int]$Games   = 100000,                        # toplam oyun (iscilere bolunur)
    [int]$Depth   = 8,                             # her hamlede sabit arama derinligi
    [int]$Workers = [Environment]::ProcessorCount, # paralel surec sayisi
    [int]$Seed    = 1,                             # taban tohum (isci basina +1)
    [string]$Out    = "build-release\texel-data.txt",
    [string]$Engine = "build-release\chess-working.exe",
    [switch]$Build                                 # once build-version.ps1 ile derle
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Build) {
    Write-Output "Calisan agac derleniyor (label=working)..."
    & (Join-Path $scriptDir "build-version.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Error "Derleme basarisiz" }
}

if (-not (Test-Path $Engine)) {
    Write-Error "Motor bulunamadi: $Engine  (once -Build ile ya da build-version.ps1 ile derleyin)"
}
if ($Workers -lt 1) { $Workers = 1 }

# Cikti dizinini garanti et.
$outDir = Split-Path -Parent $Out
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# Oyunlari iscilere pay et (son isci artigi alir).
$per  = [math]::Floor($Games / $Workers)
$rem  = $Games - ($per * $Workers)

$shards = New-Object System.Collections.Generic.List[string]
$procs  = New-Object System.Collections.Generic.List[object]

Write-Output "Baslatiliyor: $Games oyun, derinlik $Depth, $Workers isci."
$t0 = Get-Date

for ($i = 0; $i -lt $Workers; $i++) {
    $g = $per; if ($i -eq ($Workers - 1)) { $g += $rem }
    if ($g -le 0) { continue }
    $shard = "$Out.part$i"
    $shards.Add($shard)
    $s = $Seed + $i
    # datagen stderr'e ozet yazar; stdout kullanmaz. Shard yolu argv[3].
    $p = Start-Process -FilePath $Engine -ArgumentList @("datagen", "$g", $shard, "$s", "$Depth") `
                       -NoNewWindow -PassThru
    $procs.Add($p)
    Write-Output "  isci ${i}: $g oyun, tohum $s -> $shard (pid $($p.Id))"
}

Write-Output "Isciler calisiyor, bekleniyor..."
$procs | ForEach-Object { $_.WaitForExit() }

# Shard'lari tek dosyada birlestir (basit kopya; siralama onemsiz).
if (Test-Path $Out) { Remove-Item $Out }
$total = 0
$sw = [System.IO.StreamWriter]::new($Out, $false, [System.Text.Encoding]::ASCII)
foreach ($shard in $shards) {
    if (-not (Test-Path $shard)) { Write-Warning "Shard yok: $shard"; continue }
    foreach ($line in [System.IO.File]::ReadLines($shard)) {
        $sw.WriteLine($line); $total++
    }
    Remove-Item $shard
}
$sw.Close()

$dt = (Get-Date) - $t0
Write-Output ""
Write-Output "TAMAM: $total pozisyon -> $Out  ($([math]::Round($dt.TotalMinutes,1)) dk)"
