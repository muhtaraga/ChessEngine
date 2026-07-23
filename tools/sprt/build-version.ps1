# Etiketli bir RELEASE motor binary'si uretir: build-release\chess-<label>.exe
#
# SPRT icin iki surumu (yeni vs baz) karsilastirmak gerekir. Bu betik ya mevcut
# calisma agacini ya da verilen bir git ref'ini (worktree ile izole) /O2 Release
# olarak derleyip etiketli bir exe'ye kopyalar. Testler kapatilir (BUILD_TESTS=OFF)
# -> gtest indirmesi/derlemesi yok, hizli.
#
# Kullanim (proje kokunden):
#   powershell -File tools\sprt\build-version.ps1 -Ref HEAD~1 -Label base
#   powershell -File tools\sprt\build-version.ps1               # calisma agaci -> chess-working.exe
#
# NOT: MSVC PATH'te olmadigindan VS DevShell yuklenir (bkz. -VsPath). Bu makineye
# gore varsayilan ayarli; farkli kurulumda -VsPath verin.

param(
    [string]$Ref    = "",   # git ref (bos = mevcut calisma agaci)
    [string]$Label  = "",   # cikti etiketi (bos = ref sanitized ya da "working")
    [string]$VsPath = "C:\Program Files\Microsoft Visual Studio\18\Community"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path

# --- Etiketi coz ---
if ([string]::IsNullOrWhiteSpace($Label)) {
    if ([string]::IsNullOrWhiteSpace($Ref)) { $Label = "working" }
    else { $Label = ($Ref -replace '[^A-Za-z0-9_.-]', '_') }
}

$outExe = Join-Path $repo "build-release\chess-$Label.exe"
New-Item -ItemType Directory -Force -Path (Join-Path $repo "build-release") | Out-Null

# --- VS DevShell (MSVC + cmake + ninja PATH'e gelsin) ---
Import-Module "$VsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $VsPath -Arch amd64 -SkipAutomaticLocation | Out-Null

# --- Kaynak dizini: worktree (ref verildiyse) ya da calisma agaci ---
$worktree = $null
if (-not [string]::IsNullOrWhiteSpace($Ref)) {
    $worktree = Join-Path $env:TEMP "chess-wt-$Label"
    if (Test-Path $worktree) { git -C $repo worktree remove --force $worktree }
    git -C $repo worktree add --detach $worktree $Ref
    if ($LASTEXITCODE -ne 0) { throw "git worktree add basarisiz: $Ref" }
    $srcDir = $worktree
} else {
    $srcDir = $repo
}

try {
    # Build dizini daima gitignore'lu build-release altinda -> izlenmeyen dosya
    # kirlenmesi yok. -S $srcDir kaynagi (worktree ya da calisma agaci) gosterir.
    $buildDir = Join-Path $repo "build-release\_build-$Label"

    # Ayni etiket daha once BASKA bir kaynak dizininden uretildiyse (calisma agaci
    # <-> worktree gecisi) CMake "source does not match ... used to generate cache"
    # ile patlar. Cache'in kaynagini kontrol et, uyusmuyorsa build dizinini sil.
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $hit = Select-String -Path $cacheFile -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
        if ($hit) {
            $cachedSrc = $hit.Matches[0].Groups[1].Value.Replace('/', '\').TrimEnd('\')
            $wantSrc   = (Resolve-Path $srcDir).Path.Replace('/', '\').TrimEnd('\')
            if ($cachedSrc -ne $wantSrc) {
                Write-Output "Kaynak degismis ($cachedSrc -> $wantSrc); build dizini sifirlaniyor."
                Remove-Item -Recurse -Force $buildDir
            }
        }
    }

    cmake -S $srcDir -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "cmake configure basarisiz" }
    cmake --build $buildDir --target chess | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "cmake build basarisiz" }

    Copy-Item (Join-Path $buildDir "chess.exe") $outExe -Force
    Write-Output "OK: $outExe"
}
finally {
    if ($worktree -and (Test-Path $worktree)) {
        git -C $repo worktree remove --force $worktree | Out-Null
    }
}
