# Acilis kitabi (book.epd) ureticisi.
#
# Elle FEN yazmak en passant / sayac / rok hakki hatalarina cok acik (CLAUDE.md
# #4). Bunun yerine motorun kendi "chess fen <uci-moves...>" moduyla, iyi bilinen
# dengeli acilis hatlarini legal oynayip DOGRU FEN'leri urettiriyoruz. Cikti
# tools/sprt/book.epd; her satir bir baslangic pozisyonu (tam FEN). cutechess-cli
# bunu "-openings file=book.epd format=epd order=random" ile yukler; her hat iki
# tarafca da (renkler ters) oynanir -> N hat = 2N farkli oyun cekirdegi.
#
# Kullanim (proje kokunden, motor derlenmis olmali):
#   powershell -File tools/sprt/gen-book.ps1                 # build-release\chess.exe kullanir
#   powershell -File tools/sprt/gen-book.ps1 -Engine path\chess.exe

param(
    [string]$Engine = "build-release\chess.exe",
    [string]$Out    = "tools\sprt\book.epd"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Engine)) {
    Write-Error "Motor bulunamadi: $Engine  (once build-version.ps1 ile derleyin)"
}

# Dengeli, iyi bilinen acilis hatlari (UCI hamle dizileri). Her biri 2-6 ply;
# cesitlilik icin farkli aile/yapilar. Simetrik ya da tek tarafa acik hicbir hat
# yok -- SPRT'de renk yanliligini onlemek icin her hat iki renkte de oynanacak.
$lines = @(
    "e2e4 e7e5",                    # Acik oyun
    "e2e4 e7e5 g1f3 b8c6",          # Italyan/Ispanyol capraz
    "e2e4 e7e5 g1f3 g8f6",          # Petroff
    "e2e4 c7c5",                    # Sicilya
    "e2e4 c7c5 g1f3 d7d6",          # Najdorf/Scheveningen setup
    "e2e4 c7c5 b1c3",               # Kapali Sicilya
    "e2e4 e7e6",                    # Fransiz
    "e2e4 c7c6",                    # Caro-Kann
    "e2e4 d7d5",                    # Iskandinav
    "e2e4 g8f6",                    # Alekhine
    "e2e4 d7d6",                    # Pirc
    "e2e4 g7g6",                    # Modern
    "d2d4 d7d5 c2c4 e7e6",          # QGD
    "d2d4 d7d5 c2c4 c7c6",          # Slav
    "d2d4 g8f6 c2c4 e7e6",          # Nimzo/QID bolgesi
    "d2d4 g8f6 c2c4 g7g6",          # KID/Grunfeld bolgesi
    "d2d4 g8f6 g1f3 g7g6",          # Kapali Hint
    "d2d4 f7f5",                    # Hollanda
    "c2c4 e7e5",                    # Ingiliz (ters Sicilya)
    "c2c4 c7c5 g1f3 g8f6",          # Simetrik Ingiliz
    "g1f3 d7d5 c2c4",               # Reti
    "g1f3 g8f6 c2c4 g7g6"           # Hint transpozisyon
)

$fens = New-Object System.Collections.Generic.List[string]
foreach ($line in $lines) {
    $moves = $line -split '\s+'
    $fen = (& $Engine fen @moves) | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($fen)) {
        Write-Error "FEN uretimi basarisiz: '$line' -> '$fen'"
    }
    $fens.Add($fen.Trim())
}

# Yinelenenleri ele (farkli hatlar ayni pozisyona transpoze olabilir).
$unique = $fens | Select-Object -Unique
Set-Content -Path $Out -Value $unique -Encoding ascii
Write-Output "Yazildi: $Out  ($($unique.Count) benzersiz pozisyon / $($lines.Count) hat)"
