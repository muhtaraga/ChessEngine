# run-pipeline.ps1 — GUI'nin arka planda calistirdigi zincir:
#   1) base surumu derle  2) new surumu derle  3) sprt.ps1 ile maci oynat
# Tum cikti (derleme + cutechess) tek bir log dosyasina yazilir; GUI sunucusu
# bu log'un kuyrugunu okuyarak canli ilerlemeyi gosterir. Her ana adim log'a
# "### PHASE <ad>" satiri basar; sunucu fazi bu satirdan cikarir.
#
# Bu script'i normalde elle cagirmazsiniz — serve.ps1 (/api/run) baslatir.
param(
    [Parameter(Mandatory=$true)][string]$BaseRef,     # base commit (git ref)
    [Parameter(Mandatory=$true)][string]$BaseLabel,   # base binary etiketi
    [string]$NewRef      = "",                          # new commit; bos = calisan agac
    [Parameter(Mandatory=$true)][string]$NewLabel,     # new binary etiketi
    [int]   $Concurrency = 4,
    [string]$Tc          = "10+0.1",
    [double]$Elo0        = 0,
    [double]$Elo1        = 5,
    [double]$Alpha       = 0.05,
    [double]$Beta        = 0.05,
    [int]   $Rounds      = 5000,
    [int]   $Hash        = 16,
    [int]   $NewThreads  = 0,                           # Lazy SMP olcekleme: yeni motor thread (0=ayarlama)
    [int]   $BaseThreads = 0,                           # baz motor thread (0=ayarlama)
    [switch]$ForceRebuild,
    [string]$Cutechess   = "",
    [Parameter(Mandatory=$true)][string]$LogFile      # tum ciktinin yazilacagi dosya
)

# NOT: $ErrorActionPreference'i "Continue" tutuyoruz ki bir adim patlarsa bile
# faz/hata mesajlari log'a yazilabilsin (asagida her adimi elle kontrol ediyoruz).
$ErrorActionPreference = "Continue"
$repo = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$buildScript = Join-Path $repo "tools\sprt\build-version.ps1"
$sprtScript  = Join-Path $repo "tools\sprt\sprt.ps1"

# Log'a hem faz isaretcisi hem insan-okur satir yaz.
function Write-Phase([string]$name, [string]$msg) {
    Add-Content -Path $LogFile -Value "### PHASE $name" -Encoding utf8
    if ($msg) { Add-Content -Path $LogFile -Value $msg -Encoding utf8 }
}

# Bir alt-script'i calistirip ciktisini CANLI (satir satir) log'a akit; cikis
# kodunu dondur. Ayri bir powershell process'i olarak calistiriyoruz ki
# Enter-VsDevShell gibi ortam-degistiren adimlar bu surecin state'ini kirletmesin.
#
# ONEMLI: cutechess maci saatlerce surebilir ve karar verilene kadar CIKMAZ. Bu
# yuzden ciktiyi "process bitince topla" YERINE, stdout'u satir satir okuyup aninda
# log'a yaziyoruz -> GUI canli ilerlemeyi (W-D-L, Elo, LLR) gercek zamanli gorur.
# stderr sonda toplu eklenir (sadece hata durumunda dolar, kucuk).
function Invoke-Logged([string]$file, [string[]]$scriptArgs) {
    $argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $file) + $scriptArgs
    # PS 5.1: bosluklu yollari (orn. cutechess "Cute Chess" klasoru) tirnakla.
    $argStr = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName               = "powershell"
    $psi.Arguments              = $argStr
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $p = [System.Diagnostics.Process]::Start($psi)

    # stdout'u EOF'a (process stdout'u kapatana) kadar satir satir akit.
    while ($null -ne ($line = $p.StandardOutput.ReadLine())) {
        Add-Content -Path $LogFile -Value $line -Encoding utf8
    }
    $p.WaitForExit()
    $err = $p.StandardError.ReadToEnd()
    if (-not [string]::IsNullOrWhiteSpace($err)) {
        Add-Content -Path $LogFile -Value $err.TrimEnd() -Encoding utf8
    }
    return $p.ExitCode
}

try {
    $newExe  = Join-Path $repo "build-release\chess-$NewLabel.exe"
    $baseExe = Join-Path $repo "build-release\chess-$BaseLabel.exe"

    # --- 1) BASE derle (varsa ve zorlanmadiysa atla) ---
    Write-Phase "building-base" "Base derleniyor: ref=$BaseRef label=$BaseLabel"
    if ((Test-Path $baseExe) -and -not $ForceRebuild) {
        Add-Content -Path $LogFile -Value "Onbellek: $baseExe zaten var, derleme atlandi." -Encoding utf8
    } else {
        $code = Invoke-Logged $buildScript @("-Ref", $BaseRef, "-Label", $BaseLabel)
        if ($code -ne 0 -or -not (Test-Path $baseExe)) {
            Write-Phase "error" "Base derlemesi basarisiz (exit=$code)."
            exit 1
        }
    }

    # --- 2) NEW derle. Calisan agac (NewRef bos) DAIMA yeniden derlenir:
    #        kaydedilmemis degisiklikler olabilir. Commit ise onbellek gecerli. ---
    Write-Phase "building-new" "New derleniyor: ref=$(if($NewRef){$NewRef}else{'(calisan agac)'}) label=$NewLabel"
    $newIsWorkingTree = [string]::IsNullOrWhiteSpace($NewRef)
    if ((Test-Path $newExe) -and -not $ForceRebuild -and -not $newIsWorkingTree) {
        Add-Content -Path $LogFile -Value "Onbellek: $newExe zaten var, derleme atlandi." -Encoding utf8
    } else {
        # NewRef bos ise build-version.ps1 calisan agaci derler (-Ref vermeyiz).
        $buildArgs = if ($newIsWorkingTree) { @("-Label", $NewLabel) } else { @("-Ref", $NewRef, "-Label", $NewLabel) }
        $code = Invoke-Logged $buildScript $buildArgs
        if ($code -ne 0 -or -not (Test-Path $newExe)) {
            Write-Phase "error" "New derlemesi basarisiz (exit=$code)."
            exit 1
        }
    }

    # --- 3) SPRT maci ---
    Write-Phase "matching" "cutechess-cli maci basliyor..."
    $sprtArgs = @(
        "-New", $NewLabel, "-Base", $BaseLabel,
        "-Tc", $Tc, "-Concurrency", "$Concurrency",
        "-Elo0", "$Elo0", "-Elo1", "$Elo1",
        "-Alpha", "$Alpha", "-Beta", "$Beta",
        "-Rounds", "$Rounds", "-Hash", "$Hash"
    )
    if ($NewThreads  -gt 0) { $sprtArgs += @("-NewThreads",  "$NewThreads") }
    if ($BaseThreads -gt 0) { $sprtArgs += @("-BaseThreads", "$BaseThreads") }
    if (-not [string]::IsNullOrWhiteSpace($Cutechess)) { $sprtArgs += @("-Cutechess", $Cutechess) }
    $code = Invoke-Logged $sprtScript $sprtArgs
    if ($code -ne 0) {
        Write-Phase "error" "SPRT maci hata ile bitti (exit=$code)."
        exit 1
    }

    Write-Phase "done" "Tamamlandi."
    exit 0
}
catch {
    Write-Phase "error" ("Beklenmeyen hata: " + $_.Exception.Message)
    exit 1
}
