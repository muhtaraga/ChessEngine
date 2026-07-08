# SPRT Maç / Regresyon Altyapısı

Bu klasör, motorun iki sürümünü birbirine karşı oynatıp **Elo farkını SPRT ile**
karara bağlayan araçları içerir. Faz 2C'den (selective search) itibaren **her
arama/eval değişikliği bu kapıdan geçer**: düğüm sayısı düşüşü "iyileşme" sayılmaz;
yalnızca yeni sürümün baz sürüme karşı ölçülen pozitif Elo'su (SPRT'nin H1'i kabul
etmesi) iyileşme sayılır.

## Dosyalar

| Dosya | Görev |
|-------|-------|
| `build-version.ps1` | Bir git ref'ini (ya da çalışma ağacını) `/O2` Release derleyip `build-release\chess-<label>.exe` üretir. |
| `gen-book.ps1` | Açılış kitabını (`book.epd`) motorun `chess fen` moduyla **doğru** üretir (elle FEN yazma hatası yok). |
| `book.epd` | 22 dengeli açılış pozisyonu (üretilmiş; commit'li). |
| `sprt.ps1` | İki etiketli binary'yi cutechess-cli ile oynatıp SPRT kararı raporlar. |
| `gui/` | **Komutsuz web arayüzü** — tarayıcıdan sürüm seç, izlek/zaman ayarla, başlat/durdur, canlı izle. |

## Hızlı yol: GUI (komut yazmadan)

Komut ezberlemek istemiyorsanız yerel web arayüzünü kullanın:

1. `tools\sprt\gui\start-gui.cmd`'ye **çift tıklayın**.
2. Tarayıcı otomatik açılır (`http://127.0.0.1:5000`). Base commit'i seçin, izlek
   sayısını (paralel oyun) ve zaman kontrolünü ayarlayın, **Testi başlat**'a basın.
3. Aşağıdaki panelde W–D–L, Elo ± hata, LOS ve SPRT LLR canlı akar; **Durdur** ile
   kesebilir, **PGN'i aç** ile oyunları görebilirsiniz.

Ek kurulum gerekmez (Windows PowerShell 5.1'in gömülü .NET'iyle çalışır). Derleme +
maç zincirini (`build-version.ps1` → `sprt.ps1`) arka planda sizin yerinize çağırır;
cutechess-cli yine de kurulu olmalıdır (aşağıya bakın — arayüz kurulu değilse uyarır
ve yolu elle girmenize izin verir). Elle script akışı aşağıda referans olarak durur.

## Ön koşul: cutechess-cli kurulumu

cutechess-cli bu makinede **kurulu** (`C:\Program Files (x86)\Cute Chess\cutechess-cli.exe`);
hem `sprt.ps1` hem GUI onu otomatik bulur. Yeni bir makinede kurmak gerekirse (Cute
Chess GUI paketi cutechess-cli.exe'yi de içerir):

1. https://github.com/cutechess/cutechess/releases adresinden Windows installer'ını
   indir (ör. `cutechess-1.3.1-win64.msi`) ve kur.
2. Kurulum sonrası `cutechess-cli.exe` genelde `C:\Program Files\Cute Chess\` ya da
   `C:\Program Files (x86)\Cute Chess\` altında olur; `sprt.ps1` bu konumları otomatik
   arar. Farklıysa `-Cutechess <yol>` ver (GUI'de de elle yol girilebilir).

> **Alternatif (daha hafif):** [fastchess](https://github.com/Disservin/fastchess)
> tek taşınabilir exe'dir, SPRT'yi destekler ve modern motor geliştirmede yaygındır.
> `sprt.ps1` cutechess-cli argüman düzenine göre yazıldı; fastchess'e geçilirse
> argümanlar büyük ölçüde aynıdır ama `-each tc=` / `-sprt` sözdizimi kontrol edilmeli.

## İş akışı

Proje kökünden (PowerShell):

```powershell
# 1) Karşılaştırılacak iki sürümü derle (etiketli Release binary'ler).
#    Örn: tapered eval'in Elo katkısını ölç -> tapered commit'i vs bir önceki.
powershell -File tools\sprt\build-version.ps1 -Ref d7e6754 -Label base   # tapered'dan önce
powershell -File tools\sprt\build-version.ps1 -Ref HEAD    -Label new    # güncel

# 2) Açılış kitabını üret (bir kez; book.epd zaten commit'li, gerekmiyorsa atla).
powershell -File tools\sprt\gen-book.ps1

# 3) SPRT maçını başlat. Karara varınca (H0 ya da H1) otomatik durur.
powershell -File tools\sprt\sprt.ps1 -New new -Base base
```

> Bu makinede **Windows PowerShell 5.1** var (`powershell`), PowerShell 7 (`pwsh`)
> kurulu değil — komutlarda `powershell` kullanın. Zaten bir PowerShell oturumundaysanız
> betiği doğrudan `.\tools\sprt\build-version.ps1 ...` ile de çağırabilirsiniz.
> "running scripts is disabled" hatası alırsanız bir kez:
> `Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned`.

## SPRT parametreleri

- **H0 (elo0):** yeni sürüm ≤ baz + `Elo0`  (varsayılan 0 — "iyileşme yok")
- **H1 (elo1):** yeni sürüm ≥ baz + `Elo1`  (varsayılan 5 — "en az 5 Elo kazanç")
- **alpha/beta:** tip-I / tip-II hata (varsayılan 0.05 / 0.05)

cutechess LLR'yi (log-likelihood ratio) canlı raporlar; `[0.05, 0.05]` için karar
sınırları ≈ **−2.94 / +2.94**. LLR üst sınırı geçerse **H1 kabul** (yeni sürüm
kanıtlanmış şekilde daha güçlü, commit'i tut); alt sınırı geçerse **H0 kabul** (kazanç
yok/regresyon, değişikliği geri al).

Küçük/emin değişiklikler için `-Elo0 0 -Elo1 5`; büyük sıçramalar beklenen özellikler
(ör. LMR) için `-Elo0 0 -Elo1 10` daha hızlı sonuç verir.

## Zaman kontrolü ve süre

Varsayılan `tc=10+0.1` (10 sn + 0.1 sn artım). Daha hızlı iterasyon için `-Tc "5+0.05"`.
SPRT genellikle birkaç yüz–birkaç bin oyunda karara varır; `-Concurrency` CPU çekirdek
sayısına göre ayarlanır. Determinizm gerektiren (düğüm sayısı iddialı) testler tek
thread çalışır — SPRT maçları çok-oyunlu, o kısıt buraya uygulanmaz.
