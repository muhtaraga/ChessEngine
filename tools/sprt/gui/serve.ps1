# serve.ps1 - SPRT GUI'nin yerel web sunucusu.
#
# Windows PowerShell 5.1'in gomulu .NET'iyle calisir; HARICI KURULUM GEREKMEZ.
# HttpListener yerine TcpListener kullaniyoruz: http://localhost binding'i urlacl/
# admin isteyebilir, TcpListener + elle minimal HTTP/1.1 yaniti tamamen izinsiz
# calisir. Tek kullanicilik yerel arac oldugu icin tek-thread bloklayan dongu yeterli.
#
# Baslatma: start-gui.cmd'ye cift tiklayin (bu script'i cagirir ve tarayiciyi acar).
# Sunucuyu durdurmak: bu konsol penceresini kapatin ya da Ctrl+C.
param(
    [int]$Port = 5000              # mesgulse otomatik artirilir (5000..5020)
)

$ErrorActionPreference = "Stop"
$guiDir    = $PSScriptRoot
$repo      = (Resolve-Path "$guiDir\..\..\..").Path
$buildRel  = Join-Path $repo "build-release"
$logFile   = Join-Path $buildRel "gui-run.log"
$stateFile = Join-Path $buildRel "gui-run.state.json"
$configFile= Join-Path $guiDir "config.json"
$indexFile = Join-Path $guiDir "index.html"

if (-not (Test-Path $buildRel)) { New-Item -ItemType Directory -Force -Path $buildRel | Out-Null }

# ------------------------------------------------------------------ yardimcilar

# Ref'i build-version.ps1 ile AYNI kuralla dosya-guvenli etikete cevir.
function Get-SafeLabel([string]$s) { return ($s -replace '[^A-Za-z0-9_.-]', '_') }

# cutechess-cli'yi bul (sprt.ps1'deki ayni aday-yol mantigi).
function Find-Cutechess([string]$override) {
    if (-not [string]::IsNullOrWhiteSpace($override) -and (Test-Path $override)) {
        return $override
    }
    $cc = Get-Command cutechess-cli -ErrorAction SilentlyContinue
    if ($cc) { return $cc.Source }
    $cand = @(
        "C:\Program Files\Cute Chess\cutechess-cli.exe",
        "C:\Program Files (x86)\Cute Chess\cutechess-cli.exe",
        "$env:LOCALAPPDATA\Programs\Cute Chess\cutechess-cli.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($cand) { return $cand }
    return ""
}

function Get-PhysicalCores {
    try {
        $n = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum
        if ($n -gt 0) { return [int]$n }
    } catch { }
    # Yedek: mantiksal islemci sayisi
    return [int]$env:NUMBER_OF_PROCESSORS
}

function Load-Config {
    if (Test-Path $configFile) {
        try { return (Get-Content $configFile -Raw -Encoding utf8 | ConvertFrom-Json) } catch { }
    }
    return [PSCustomObject]@{}
}
function Save-Config($obj) {
    try { $obj | ConvertTo-Json -Depth 8 | Set-Content -Path $configFile -Encoding utf8 } catch { }
}
function Load-State {
    if (Test-Path $stateFile) {
        try { return (Get-Content $stateFile -Raw -Encoding utf8 | ConvertFrom-Json) } catch { }
    }
    return $null
}
function Test-RunAlive($state) {
    if ($null -eq $state -or -not $state.pid) { return $false }
    return [bool](Get-Process -Id ([int]$state.pid) -ErrorAction SilentlyContinue)
}

# Calisan derlemis chess-<label>.exe etiketlerini listele.
function Get-BuiltLabels {
    $out = @()
    if (Test-Path $buildRel) {
        Get-ChildItem -Path $buildRel -Filter "chess-*.exe" -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.BaseName -match '^chess-(.+)$') { $out += $Matches[1] }
        }
    }
    return $out
}

# --- HTTP okuma/yazma (ham TCP uzerinde minimal HTTP/1.1) ---

function Find-DoubleCRLF([byte[]]$a) {
    for ($i = 0; $i -le $a.Length - 4; $i++) {
        if ($a[$i] -eq 13 -and $a[$i+1] -eq 10 -and $a[$i+2] -eq 13 -and $a[$i+3] -eq 10) { return $i }
    }
    return -1
}

function Read-HttpRequest($stream) {
    try { $stream.ReadTimeout = 8000 } catch { }
    $ms  = New-Object System.IO.MemoryStream
    $buf = New-Object 'byte[]' 8192
    $headerLen = -1; $contentLength = 0; $bodyStart = 0
    try {
        while ($true) {
            if ($headerLen -ge 0) {
                if (($ms.Length - $bodyStart) -ge $contentLength) { break }
            } else {
                $arr = $ms.ToArray()
                $i = Find-DoubleCRLF $arr
                if ($i -ge 0) {
                    $headerLen = $i; $bodyStart = $i + 4
                    $htxt = [System.Text.Encoding]::ASCII.GetString($arr, 0, $i)
                    $m = [regex]::Match($htxt, '(?im)^Content-Length:\s*(\d+)')
                    if ($m.Success) { $contentLength = [int]$m.Groups[1].Value }
                    if (($ms.Length - $bodyStart) -ge $contentLength) { break }
                }
            }
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $ms.Write($buf, 0, $n)
        }
    } catch { }
    $all = $ms.ToArray()
    if ($headerLen -lt 0) { $headerLen = $all.Length; $bodyStart = $all.Length }
    $headerText = [System.Text.Encoding]::ASCII.GetString($all, 0, $headerLen)
    $body = ""
    if ($contentLength -gt 0 -and $all.Length -ge ($bodyStart + $contentLength)) {
        $body = [System.Text.Encoding]::UTF8.GetString($all, $bodyStart, $contentLength)
    }
    return @{ Header = $headerText; Body = $body }
}

function Send-Response($stream, [int]$code, [string]$contentType, [byte[]]$body) {
    $statusText = @{ 200="OK"; 400="Bad Request"; 404="Not Found"; 409="Conflict"; 500="Internal Server Error" }[$code]
    if (-not $statusText) { $statusText = "OK" }
    $header = "HTTP/1.1 $code $statusText`r`n" +
              "Content-Type: $contentType`r`n" +
              "Content-Length: $($body.Length)`r`n" +
              "Cache-Control: no-store`r`n" +
              "Connection: close`r`n`r`n"
    $hb = [System.Text.Encoding]::ASCII.GetBytes($header)
    $stream.Write($hb, 0, $hb.Length)
    if ($body.Length -gt 0) { $stream.Write($body, 0, $body.Length) }
    $stream.Flush()
}
function Send-Json($stream, [int]$code, $obj) {
    $json = $obj | ConvertTo-Json -Depth 8 -Compress
    Send-Response $stream $code "application/json; charset=utf-8" ([System.Text.Encoding]::UTF8.GetBytes($json))
}
function Send-Text($stream, [int]$code, [string]$text, [string]$ctype) {
    Send-Response $stream $code $ctype ([System.Text.Encoding]::UTF8.GetBytes($text))
}

# --- cutechess log satirlarini yapisal veriye cevir ---
function Parse-Progress {
    $result = [ordered]@{
        phase   = "idle"
        running = $false
        wins    = $null; losses = $null; draws = $null; games = $null
        elo     = $null; eloErr = $null; los = $null
        llr     = $null; llrLower = $null; llrUpper = $null; decision = $null
        tail    = ""
    }
    if (-not (Test-Path $logFile)) { return $result }

    $lines = @()
    try { $lines = Get-Content $logFile -Tail 250 -ErrorAction SilentlyContinue } catch { }
    $result.tail = ($lines -join "`n")

    # Faz: son "### PHASE x" satiri
    $phaseLine = ($lines | Where-Object { $_ -match '^### PHASE ' } | Select-Object -Last 1)
    if ($phaseLine -and $phaseLine -match '^### PHASE (\S+)') { $result.phase = $Matches[1] }

    # Skor: "Score of NEW vs BASE: 40 - 30 - 50  [0.54] 120"
    $scoreLine = ($lines | Where-Object { $_ -match 'Score of .+?:\s*\d+ - \d+ - \d+' } | Select-Object -Last 1)
    if ($scoreLine -and $scoreLine -match 'Score of .+?:\s*(\d+) - (\d+) - (\d+)\s*(?:\[[\d.]+\])?\s*(\d+)?') {
        $result.wins   = [int]$Matches[1]
        $result.losses = [int]$Matches[2]
        $result.draws  = [int]$Matches[3]
        if ($Matches[4]) { $result.games = [int]$Matches[4] }
        else { $result.games = $result.wins + $result.losses + $result.draws }
    }

    # Elo: "Elo difference: 28.9 +/- 30.1, LOS: 96.9 %"
    $eloLine = ($lines | Where-Object { $_ -match 'Elo difference:' } | Select-Object -Last 1)
    if ($eloLine -and $eloLine -match 'Elo difference:\s*(-?[\d.]+)\s*\+/-\s*(-?[\d.]+|nan|inf)') {
        # DIKKAT: asagidaki ic-ice -match otomatik $Matches'i ezer; grup degerlerini
        # once yerel degiskenlere al, sonra kontrol et.
        $eloVal = $Matches[1]; $errVal = $Matches[2]
        $result.elo = [double]$eloVal
        if ($errVal -match '^-?[\d.]+$') { $result.eloErr = [double]$errVal }
    }
    if ($eloLine -and $eloLine -match 'LOS:\s*([\d.]+)\s*%') { $result.los = [double]$Matches[1] }

    # LLR: "SPRT: llr 1.23 (12.5%), lag 0.0, elo0 0.0 elo1 5.0"
    $llrLine = ($lines | Where-Object { $_ -match 'llr\s' } | Select-Object -Last 1)
    if ($llrLine -and $llrLine -match 'llr\s+(-?[\d.]+)') { $result.llr = [double]$Matches[1] }

    # SPRT karar sinirlari: state'teki alpha/beta'dan
    $state = Load-State
    $alpha = 0.05; $beta = 0.05
    if ($state -and $state.config) {
        if ($state.config.alpha) { $alpha = [double]$state.config.alpha }
        if ($state.config.beta)  { $beta  = [double]$state.config.beta }
    }
    $upper = [math]::Log((1 - $beta) / $alpha)
    $lower = [math]::Log($beta / (1 - $alpha))
    $result.llrUpper = [math]::Round($upper, 2)
    $result.llrLower = [math]::Round($lower, 2)
    if ($null -ne $result.llr) {
        if ($result.llr -ge $upper)     { $result.decision = "H1 kabul - yeni surum daha guclu (commit'i tut)" }
        elseif ($result.llr -le $lower) { $result.decision = "H0 kabul - iyilesme yok (geri al)" }
        else                            { $result.decision = "Devam ediyor" }
    }

    $result.running = (Test-RunAlive $state) -and ($result.phase -ne "done") -and ($result.phase -ne "error")
    return $result
}

# ------------------------------------------------------------------ API islemleri

function Handle-Status {
    $cfg = Load-Config
    $ccOverride = if ($cfg.cutechess) { [string]$cfg.cutechess } else { "" }
    $ccPath = Find-Cutechess $ccOverride
    $state = Load-State
    return @{
        physicalCores = Get-PhysicalCores
        logicalCores  = [int]$env:NUMBER_OF_PROCESSORS
        cutechess     = @{ found = [bool]$ccPath; path = $ccPath }
        bookExists    = (Test-Path (Join-Path $repo "tools\sprt\noob_2moves.epd"))
        builtLabels   = @(Get-BuiltLabels)
        running       = (Test-RunAlive $state)
        lastConfig    = $cfg
    }
}

function Handle-Commits {
    $list = @()
    try {
        $raw = & git -C $repo log --pretty=format:"%h|%s|%ci" -n 60 2>$null
        foreach ($line in $raw) {
            $parts = $line -split '\|', 3
            if ($parts.Count -ge 2) {
                $date = if ($parts.Count -ge 3) { ($parts[2] -split ' ')[0] } else { "" }
                $list += @{ hash = $parts[0]; subject = $parts[1]; date = $date }
            }
        }
    } catch { }
    return @{ commits = @($list) }
}

function Handle-Run($body) {
    $state = Load-State
    if (Test-RunAlive $state) {
        return @{ code = 409; obj = @{ ok = $false; error = "Zaten calisan bir test var." } }
    }
    $cfg = $body
    if ([string]::IsNullOrWhiteSpace([string]$cfg.baseRef)) {
        return @{ code = 400; obj = @{ ok = $false; error = "Base commit secilmedi." } }
    }

    $baseRef   = [string]$cfg.baseRef
    $newRef    = if ($cfg.newRef) { [string]$cfg.newRef } else { "" }   # bos = calisan agac
    $baseLabel = Get-SafeLabel $baseRef
    $newLabel  = if ([string]::IsNullOrWhiteSpace($newRef)) { "working" } else { Get-SafeLabel $newRef }

    # Thread sayilari (Lazy SMP olcekleme). Guard'dan ONCE lazim.
    $newThreads  = if ($null -ne $cfg.newThreads)  { [int]$cfg.newThreads }  else { 0 }
    $baseThreads = if ($null -ne $cfg.baseThreads) { [int]$cfg.baseThreads } else { 0 }

    if ($baseLabel -eq $newLabel) {
        # Ayni surum + FARKLI thread = mesru Lazy SMP olcekleme testi (ayni kod, thread
        # farki). Etiketleri thread sayisiyla ayristir -> iki ozdes binary, farkli motor
        # ismi (cutechess isim cakismaz). Aksi halde (thread de ayni) gercekten anlamsiz.
        $ntEff = if ($newThreads  -gt 0) { $newThreads }  else { 1 }
        $btEff = if ($baseThreads -gt 0) { $baseThreads } else { 1 }
        if ($ntEff -ne $btEff) {
            $newLabel  = "$newLabel-${ntEff}t"
            $baseLabel = "$baseLabel-${btEff}t"
        } else {
            return @{ code = 400; obj = @{ ok = $false; error = "Base ve New ayni surumu ($baseLabel) VE ayni thread'i gosteriyor; farkli surum ya da farkli thread secin." } }
        }
    }

    # cutechess yolu (override -> config -> otomatik)
    $ccOverride = if ($cfg.cutechess) { [string]$cfg.cutechess } elseif ((Load-Config).cutechess) { [string](Load-Config).cutechess } else { "" }
    $ccPath = Find-Cutechess $ccOverride

    # Sayisal alanlar icin guvenli varsayilanlar
    $concurrency = if ($cfg.concurrency) { [int]$cfg.concurrency } else { 4 }
    $tc          = if ($cfg.tc) { [string]$cfg.tc } else { "10+0.1" }
    $elo0        = if ($null -ne $cfg.elo0) { [double]$cfg.elo0 } else { 0 }
    $elo1        = if ($null -ne $cfg.elo1) { [double]$cfg.elo1 } else { 5 }
    $alpha       = if ($null -ne $cfg.alpha) { [double]$cfg.alpha } else { 0.05 }
    $beta        = if ($null -ne $cfg.beta) { [double]$cfg.beta } else { 0.05 }
    $rounds      = if ($cfg.rounds) { [int]$cfg.rounds } else { 5000 }
    $hash        = if ($null -ne $cfg.hash) { [int]$cfg.hash } else { 16 }
    $force       = [bool]$cfg.forceRebuild

    # Log'u sifirla
    "### PHASE starting" | Set-Content -Path $logFile -Encoding utf8
    Add-Content -Path $logFile -Value "SPRT GUI kosusu: new=$newLabel base=$baseLabel concurrency=$concurrency tc=$tc" -Encoding utf8

    # Pipeline'i gizli, ayri process olarak baslat
    $pipeline = Join-Path $guiDir "run-pipeline.ps1"
    $plArgs = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $pipeline,
        "-BaseRef", $baseRef, "-BaseLabel", $baseLabel, "-NewLabel", $newLabel,
        "-Concurrency", "$concurrency", "-Tc", $tc,
        "-Elo0", "$elo0", "-Elo1", "$elo1", "-Alpha", "$alpha", "-Beta", "$beta",
        "-Rounds", "$rounds", "-Hash", "$hash",
        "-LogFile", $logFile
    )
    if (-not [string]::IsNullOrWhiteSpace($newRef))  { $plArgs += @("-NewRef", $newRef) }
    if (-not [string]::IsNullOrWhiteSpace($ccPath))  { $plArgs += @("-Cutechess", $ccPath) }
    if ($newThreads  -gt 0) { $plArgs += @("-NewThreads",  "$newThreads") }
    if ($baseThreads -gt 0) { $plArgs += @("-BaseThreads", "$baseThreads") }
    if ($force) { $plArgs += "-ForceRebuild" }

    # PS 5.1'de Start-Process -ArgumentList bir DIZIYI bosluklarla birlestirir ve
    # tirnaklamaz -> bosluklu yollar (orn. "Cute Chess") bolunur. Bosluk iceren her
    # argumani elle cift-tirnakla.
    $argStr = ($plArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $proc = Start-Process -FilePath "powershell" -ArgumentList $argStr -WindowStyle Hidden -PassThru

    $pgn = Join-Path $buildRel "sprt-$newLabel-vs-$baseLabel.pgn"
    $newState = @{
        pid       = $proc.Id
        startedAt = (Get-Date).ToString("s")
        pgn       = $pgn
        config    = @{
            baseRef = $baseRef; newRef = $newRef; baseLabel = $baseLabel; newLabel = $newLabel
            concurrency = $concurrency; tc = $tc; elo0 = $elo0; elo1 = $elo1
            alpha = $alpha; beta = $beta; rounds = $rounds; hash = $hash
        }
    }
    $newState | ConvertTo-Json -Depth 8 | Set-Content -Path $stateFile -Encoding utf8

    # Son kullanilan ayarlari config'e sakla (cutechess yolu dahil)
    $persist = Load-Config
    $persistObj = @{
        cutechess = if ($cfg.cutechess) { [string]$cfg.cutechess } elseif ($persist.cutechess) { [string]$persist.cutechess } else { "" }
        baseRef = $baseRef; newRef = $newRef; concurrency = $concurrency; tc = $tc
        elo0 = $elo0; elo1 = $elo1; alpha = $alpha; beta = $beta; rounds = $rounds; hash = $hash
    }
    Save-Config $persistObj

    return @{ code = 200; obj = @{ ok = $true; pid = $proc.Id; pgn = $pgn } }
}

function Handle-Stop {
    $state = Load-State
    if (-not (Test-RunAlive $state)) {
        return @{ ok = $false; error = "Calisan test yok." }
    }
    try {
        & taskkill /PID ([int]$state.pid) /T /F 2>&1 | Out-Null
        Add-Content -Path $logFile -Value "### PHASE stopped" -Encoding utf8
        Add-Content -Path $logFile -Value "Kullanici testi durdurdu." -Encoding utf8
        return @{ ok = $true }
    } catch {
        return @{ ok = $false; error = $_.Exception.Message }
    }
}

function Handle-Open($body) {
    $state = Load-State
    $what = if ($body.what) { [string]$body.what } else { "folder" }
    try {
        if ($what -eq "pgn" -and $state -and $state.pgn -and (Test-Path $state.pgn)) {
            Invoke-Item -Path $state.pgn
        } else {
            Invoke-Item -Path $buildRel
        }
        return @{ ok = $true }
    } catch {
        return @{ ok = $false; error = $_.Exception.Message }
    }
}

function Handle-SaveConfig($body) {
    $cur = Load-Config
    # Gelen alanlari mevcut config uzerine birlestir
    $merged = @{}
    foreach ($p in $cur.PSObject.Properties) { $merged[$p.Name] = $p.Value }
    foreach ($p in $body.PSObject.Properties) { $merged[$p.Name] = $p.Value }
    Save-Config $merged
    return @{ ok = $true }
}

# ------------------------------------------------------------------ dinleme dongusu

$listener = $null
for ($p = $Port; $p -le $Port + 20; $p++) {
    try {
        $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $p)
        $listener.Start()
        $Port = $p
        break
    } catch {
        $listener = $null
    }
}
if ($null -eq $listener) { throw "Bos port bulunamadi ($Port..$($Port+20))." }

$url = "http://127.0.0.1:$Port/"
Write-Host ""
Write-Host "  SPRT Test Arayuzu calisiyor:  $url" -ForegroundColor Green
Write-Host "  Durdurmak icin bu pencereyi kapatin (ya da Ctrl+C)." -ForegroundColor DarkGray
Write-Host ""
try { Start-Process $url } catch { }

try {
    while ($true) {
        $client = $listener.AcceptTcpClient()
        $stream = $null
        try {
            $stream = $client.GetStream()
            $req = Read-HttpRequest $stream
            $firstLine = ($req.Header -split "`r`n")[0]
            $tok = $firstLine -split ' '
            $method = $tok[0]; $path = if ($tok.Count -ge 2) { $tok[1] } else { "/" }
            # sorgu string'ini ayikla
            if ($path.Contains('?')) { $path = $path.Substring(0, $path.IndexOf('?')) }

            $bodyObj = $null
            if ($req.Body) { try { $bodyObj = $req.Body | ConvertFrom-Json } catch { $bodyObj = $null } }

            switch -Regex ("$method $path") {
                '^GET /$' {
                    if (Test-Path $indexFile) {
                        Send-Text $stream 200 (Get-Content $indexFile -Raw -Encoding utf8) "text/html; charset=utf-8"
                    } else {
                        Send-Text $stream 404 "index.html bulunamadi" "text/plain; charset=utf-8"
                    }
                }
                '^GET /api/status$'   { Send-Json $stream 200 (Handle-Status) }
                '^GET /api/commits$'  { Send-Json $stream 200 (Handle-Commits) }
                '^GET /api/progress$' { Send-Json $stream 200 (Parse-Progress) }
                '^POST /api/run$'     { $r = Handle-Run $bodyObj;  Send-Json $stream $r.code $r.obj }
                '^POST /api/stop$'    { Send-Json $stream 200 (Handle-Stop) }
                '^POST /api/open$'    { Send-Json $stream 200 (Handle-Open $bodyObj) }
                '^POST /api/saveconfig$' { Send-Json $stream 200 (Handle-SaveConfig $bodyObj) }
                default { Send-Text $stream 404 "Bulunamadi: $method $path" "text/plain; charset=utf-8" }
            }
        } catch {
            try { Send-Text $stream 500 ("Sunucu hatasi: " + $_.Exception.Message) "text/plain; charset=utf-8" } catch { }
        } finally {
            if ($stream) { try { $stream.Close() } catch { } }
            try { $client.Close() } catch { }
        }
    }
} finally {
    try { $listener.Stop() } catch { }
}
