# Satranç Motoru Projesi

Ben bir satranç motoru geliştiriyorum, hedefim Stockfish'in izlediği klasik yoldan
(alpha-beta search) başlayıp zamanla NNUE tabanlı evaluation'a geçmek. Performans
kritik olduğu için **C++** kullanacağız.

Bu bir tek seferlik kod üretimi değil, aylar sürecek iteratif bir proje. Sana rehberlik
edecek prensipler ve yol haritası aşağıda. Her oturumda bu bağlamı hatırla, ilerlemeyi
bu fazlara göre değerlendir.

**FORK (2026-07-10, `9d42bef`):** Bu repo artık **klasik taban**. NNUE işi ayrı bir
repoda yürüyor: `../ChessEngineNNUE` (github.com/muhtaraga/ChessEngineNNUE), bu reponun
git clone'u. İkisi de aktif. Buradaki Faz 3 (NNUE) maddeleri **orada** hayata geçiyor;
burada kalan iş Faz 2C-hız (pin-aware movegen) + Faz 2D (Lazy SMP). SPRT'den geçen
search/movegen commit'leri NNUE reposuna `git fetch classical && git cherry-pick <sha>`
ile taşınır. `tools/sprt/*` yalnız BURADA değiştirilir (iki kopyanın sürüklenmesi en
sinsi risk). **Faz 2D, NNUE tarafında N4'ten (incremental accumulator) ÖNCE cherry-pick
edilmeli** — N4 Board/Searcher'ı derinden değiştirip merge'i zorlaştıracak.

### Genel çalışma prensipleri

1. **Doğruluk her şeyden önce gelir.** Move generation'da tek bir bug tüm search'ü
   anlamsız kılar. Her önemli fonksiyon için test yaz, özellikle perft testleri.
2. **Küçük, doğrulanabilir adımlarla ilerle.** Her adımda çalışan, test edilmiş bir
   durum bırak. "Şunu da ekleyeyim, şunu da düzelteyim" diyerek büyük, test edilmemiş
   commit'ler yapma.
3. **Performans iddiaları ölç, varsayma.** "Bu daha hızlı olur" dediğinde benchmark
   sonucunu göster (nodes/second, perft süresi vb.).
4. **Emin olmadığın satranç kuralı / algoritma detayı varsa tahmin etme, söyle.**
   Özellikle en passant, rok hakları, promotion, threefold repetition, 50-hamle
   kuralı gibi ince kuralları atlama ya da yanlış hatırlama riski var.
5. Kod yorumları ve commit mesajları **Türkçe** olsun. Değişken/fonksiyon isimleri
   İngilizce kalabilir (endüstri standardı), ama açıklayıcı yorumlar Türkçe yazılsın.

### Faz 1 — Temel Motor (öncelik burada)

- [x] Board representation: bitboard tabanlı (0x88 veya array değil — ileride
      NNUE'ya geçerken bitboard altyapısı gerekecek, baştan doğru kur)
- [x] Move generation: piyon, at, fil, kale, vezir, şah + özel hamleler
      (rok, en passant, promotion)
- [x] Magic bitboards ile sliding piece (fil/kale/vezir) hareket üretimi
- [x] **Perft testleri**: standart perft pozisyonlarına (Kiwipete dahil) karşı
      derinlik 5-6'ya kadar doğrula, sonuçları bilinen referans değerlerle karşılaştır
- [x] Basit minimax + alpha-beta pruning
- [x] Materyal + piece-square table tabanlı basit evaluation
- [x] UCI protokolü implementasyonu (Cutechess/Arena ile test edilebilir olsun)

Faz 1'in bitiş kriteri: motor UCI üzerinden bir GUI'ye bağlanabiliyor, legal
hamlelerle oynuyor, perft testleri geçiyor.

### Faz 2 — Klasik Güçlendirme

Klasik motoru NNUE'ya geçmeden önce olabildiğince güçlendiriyoruz. Faz 2, dört
bölüme ayrılıyor. **Sıralama kritik:** 2B'deki SPRT/maç altyapısı, 2C ve 2D'deki
her şeyin ön koşulu — çünkü selective search ve multi-threading kazançları
düğüm sayısıyla değil, ancak eski versiyona karşı maç sonucuyla (Elo) doğrulanır.

**Faz 2A — Temel altyapı (TAMAM)**

- [x] Zobrist hashing + transposition table
- [x] Move ordering: MVV-LVA, killer moves, history heuristic
- [x] Quiescence search (horizon effect'i azaltmak için)
- [x] Iterative deepening + aspiration windows + time management
- [x] Asenkron stop / go infinite + UCI seçenekleri (Hash, Clear Hash)

**Faz 2B — Değerlendirme + test altyapısı (TAMAM)**

- [x] Gelişmiş evaluation: pawn structure (isole/çift/geçer piyon), king safety
      (şah bölgesi saldırıları, piyon kalkanı), piece mobility, bishop pair,
      rook on open/semi-open file. Tapered eval (middlegame/endgame interpolasyon).
- [x] **cutechess-cli ile otomatik maç + SPRT regresyon altyapısı.** Bir betik:
      iki versiyonu (yeni vs. baz) N oyun, sabit zaman kontrolünde oynatır, Elo
      farkı + hata payı + LOS/SPRT kararı (H0 reddedildi mi?) raporlar. Açılış
      kitabı (varyasyon için), tekrar/aynılık kontrolü. **Bundan sonraki her
      arama değişikliği bu kapıdan geçecek** — düğüm sayısı düşmesi "iyileşme"
      sayılmaz, sadece Elo sayılır.
- [ ] (kozmetik) `seldepth` raporlama: quiescence + extension'larla ulaşılan en
      derin ply'ı takip edip `info depth x seldepth y` bas (Stockfish gibi).

**Faz 2C — Selective search (her adım tek başına, SPRT ile doğrulanır)**

Her madde ayrı bir commit + ayrı bir SPRT koşusu. Kazanç göstermeyen ya da
regresyon veren teknik geri alınır. Sıralama, altyapı bağımlılığına göre:

- [x] **PVS (Principal Variation Search)**: ilk hamle tam pencereyle, kalanlar
      null-window [α, α+1] ile aranır; α'yı geçen olursa tam pencereyle yeniden
      aranır. LMR'nin oturduğu çerçeve — önce bu gelmeli.
- [x] **Null move pruning**: rakibe bedava hamle ver, azaltılmış derinlikte
      (R≈2-3) beta etrafında ara; sonuç ≥ beta ise dalı buda. **Dikkat:** çekteyken,
      çok sığ derinlikte ve zugzwang riskinde (yalnız şah+piyon kalınca) uygulanmaz;
      yüksek derinlikte verification search ile zugzwang blunder'ı önlenir.
- [x] **SEE (Static Exchange Evaluation)** + quiescence temizliği: bir karedeki
      taş alışverişinin materyal sonucunu hesapla. Kullanımı: (a) qsearch'te kayıplı
      yakalamaları ele, (b) delta pruning (Adım 3 notundaki ertelenen işler), (c)
      ileride LMR/futility kararlarında "taktik olarak kazançlı hamleyi budama".
- [x] **LMR (Late Move Reductions)**: move ordering'de geç gelen, quiet, çek
      olmayan hamleleri azaltılmış derinlikte ara (derinlik+sıraya bağlı log tablo);
      α'yı geçerse tam derinlikte yeniden ara. **Tek başına en büyük Elo kazancı**
      ve nominal derinliği en çok şişiren teknik (Stockfish'in bizden hızlı
      derinleşmesinin ana sebebi).
- [x] **Futility + reverse futility (static null move)**: yaprağa yakın, static
      eval ± margin ile umutsuz quiet hamleleri/dalları ele.
- [x] **Late move pruning (move-count)**: sığ derinlikte, belli sayıdan sonraki
      quiet hamleleri hiç arama.
- [x] **Razoring**: sığ derinlikte eval çok geriyse doğrudan qsearch'e düş, o da
      α'nın altındaysa buda.
- [~] **Check extension** (çek veren/çekteki hatları uzat): DENENDİ, RAFA KALDIRILDI.
      Hem naif (tüm çekler) hem SEE-kapılı (Stockfish see_ge) form SPRT'de NÖTR
      çıktı — çok-budayan yığında (LMR+null+futility+LMP+razoring) çeklerin taktik
      değeri zaten yakalanıyor. İleride opsiyonel: **singular extension** (bir hamle
      diğerlerinden belirgin iyiyse uzat) — ileri seviye, en sona.

**Faz 2C-ek — Move ordering güçlendirme (selective search'e paralel)**

- [x] **History malus (işaretli history)**: beta kesmesinde kesen quiet ödül,
      önce boşuna aranan quiet'ler ceza -> history [-max,+max] merkezli. SPRT +22.2
      Elo (erken kabul). Killer + history + MVV-LVA zaten vardı; bu onları güçlendirdi.
- [x] **Continuation history + tablo kalıcılığı** (paket, SPRT +31.6 Elo, H1 tam kabul).
      İki commit tek SPRT'de: (a) `e9821a9` cont-hist (önceki hamlenin taş+hedef
      bağlamıyla indekslenen ikinci history tablosu), (b) `ae20258` killer/history/
      cont-hist artık bir OYUN boyunca yaşıyor (ucinewgame'de temizlenir, her arama
      başında history yarılanır). Kalıcılık asıl kaldıraçtı: cont-hist tek başına
      2276 oyunda yalnız +6 ± 10.9 (kararsız) verdi, çünkü tablolar her `go`'da
      sıfırlanıyordu -> tek hamlelik aramada seyrek cont-hist dolmuyor (ölçüldü:
      LMR'ye giren quiet'lerin |stat| ortalaması startpos 131, Kiwipete 1).
- [x] **History-tabanlı LMR + history ölçek çarpanı** (SPRT +13.6 ± 8.5, LLR 2.95 tam
      kabul, 3928 oyun, LOS %99.9 — commit `9bdcef4`, YENİ BASELINE). `stat = main +
      cont` (ham); `reduction -= clamp(stat/512, -2, +2)`. Ölçek çarpanı
      `kHistoryBonusScale = 64` (sıralamayı DEĞİŞTİRMEZ — taze aramada düğüm birebir
      aynı; age()'in h/=2 ile sinyali silmesini önler ve bölene granülarite verir).
      Bölen ÖLÇÜLEREK seçildi. Mütevazı kazanç -> çok oyun istedi (razoring gibi).
- [ ] (denendi, geri alındı) **Countermove**: sert bant entegrasyonu regresyon
      verdi (LMP ile iyi hamleleri buduyordu); ileride history-bonusu olarak denenebilir.
      NOT: kalıcı history + cont-hist ile taban değişti; yeniden denenmeye aday.

**Faz 2C-hız — Pin-aware legal move generation (YENİ, yüksek beklenen değer)**

Şu an `generate_legal` (movegen.cpp) legallik testini **hamleyi oynayarak** yapıyor:
her pseudo-legal hamle için `Board next = b; next.do_move(m);` + `is_square_attacked`.
Düğüm başına ~30-40 tahta kopyası. Arama ise düğüm başına yalnız 1 kopya yapıyor —
yani kopyalama maliyetinin ezici çoğunluğu movegen'de.

İki ek israf ölçüldü/tespit edildi:
- **quiescence** (search.cpp) tüm legal hamleleri üretip sonra yalnız yakalama+
  promosyonu süzüyor -> sessiz hamleler için ödenen kopyalar tamamen çöp. Qsearch
  düğümlerin yarıdan fazlası.
- negamax'ta `Board next` **budama kararlarından önce** kuruluyor (gives_check için)
  -> futility/LMP ile atılan hamleler bile kopya + do_move bedeli ödüyor.
  Pin-aware makinenin yan ürünü **check-squares**, gives_check'i çocuk kurmadan verir.

Gerekli makine (düğüm başına bir kez): `checkers` (see.cpp'deki `attackers_to`
dışarı açılır), `blockers/pinners` (sniper algoritması: `rook_attacks(ksq,0)` /
`bishop_attacks(ksq,0)` ile boş-tahta ışınları), `check_mask` (0 çek -> tümü;
1 çek -> `between(ksq,checker) | checker`; 2 çek -> yalnız şah hamleleri).
Eksik tablolar: `between_bb[64][64]` + `line_bb[64][64]` (32 KB'er, attacks.hpp
constexpr desenine oturur). Kural: pinli taş yalnız `line_bb(ksq,pinner)` üzerinde
oynar; **çekteyken pinli taşın hiç legal hamlesi yoktur**.

Üç tuzak (perft'in yakaladığı klasikler): (a) şah hamlesi testinde şah occupancy'den
ÇIKARILMALI (yoksa çek veren ışında geriye kaçış "güvenli" görünür); (b) en passant
tek sıradan iki taş kaldırır -> pin makinesine görünmez, ayrı occupancy testi şart;
(c) çift çekte yalnız şah hamlesi. Rok zaten tam legal üretiliyor (`generate_castling`).

- [x] **Aşama 1 (DAVRANIŞ-KORUYAN, commit `bfc7aab`)**: `generate_pseudo` aynen kaldı;
      kopya-tabanlı filtre predikatla değişti. Düğüm başına bir kez `LegalityContext`
      (checkers / check_mask / pinned, sniper algoritması), sonra hamle başına sabit
      sayıda bit işlemi. `between_bb` + `line_bb` constexpr tabloları (attacks.hpp).
      Üç tuzak da ele alındı (şah occupancy'den çıkar, ep ayrı tam occupancy testi,
      çift çekte yalnız şah). Eski üreteç `generate_legal_reference` olarak korundu.
- [x] **Aşama 1b (DAVRANIŞ-KORUYAN, commit `127d4f0`)**: `generate_noisy` (yakalama +
      TÜM promosyonlar, sessiz 8. sıra itmeleri dahil), `generate_pseudo`'nun tarama
      düzeninde -> göreli sıra aynı. qsearch çekte değilken bunu kullanır (çekteyken
      `generate_legal` şart: kaçış gürültülü olmak zorunda değil + mat tespiti tüm
      hamlelerin yokluğuna bakar). BONUS (aynı exactness): `stand_pat` artık hamle
      üretiminden ÖNCE — beta kesmesinde üretilen liste tamamen çöptü.
- [x] **SPRT (Aşama 1 + 1b tek koşuda): base `4147016` vs new `127d4f0`, 186 oyun,
      W-D-L 94-63-29, Elo +126.8 ± 42, LOS %100, LLR 2.96 (tam kabul), H1 kabul.**
      Klasik fazın ÜÇÜNCÜ en büyük tekil kazancı (LMR +164.5, futility +143 ardından).
      Arama mantığı hiç değişmeden — döviz kuru (2× süre ≈ +195 Elo) doğrulandı.
      **YENİ BASELINE `127d4f0`.**

  Kapılar (üçü de geçti): (1) perft birebir — startpos d6 = 119.060.324, Kiwipete
  d5 = 193.690.690; (2) düğüm-sayısı eşitliği — taze arama d10 startpos 62.541 /
  Kiwipete 633.106, d13 startpos 627.044, d12 Kiwipete 1.590.343, bestmove+skor+PV
  baseline ile birebir; (3) diferansiyel test — perft ağacında `generate_legal` ==
  `generate_legal_reference` ve `generate_noisy` == süzülmüş `generate_legal`, AYNI
  SIRADA (ep-pin, çift çek, şah gölgesi pozisyonları dahil). 104 -> 105 test.
  Perft tek başına yetmezdi: bir legal hamle elenip bir illegal eklenirse toplam
  değişmez; diferansiyel test bu hata sınıfını kapatır.

  Ölçüm (Release, baseline -> Aşama 1 -> +1b):
  - perft startpos d6: 1.878 s -> 0.908 s (63.4M -> 131.1M nps, 2.07×)
  - perft Kiwipete d5: 2.745 s -> 1.128 s (70.3M -> 171.7M nps, 2.43×)
  - **arama** startpos d13 nps: 1.104M -> 1.624M -> 1.866M (+%69)
  - **arama** Kiwipete d12 nps: 1.053M -> 1.662M -> 2.363M (+%125)

- [ ] **Aşama 2 (opsiyonel, cazibesi AZALDI)**: tam pin-aware üretim (hedef
      maskeleriyle doğrudan legal üretim). Kopyalar Aşama 1'de zaten kalktı; geriye
      kalan kazanç yalnız sessiz hamlelerin legallik testini büsbütün atlamak.
      Üretim sırası değişir -> exact DEĞİL, ayrı commit + ayrı SPRT. Yan ürün: ucuz
      `gives_check` (negamax'ta `Board next`'i budama kararlarından önce kurma
      israfını kaldırır), SEE'ye pin bilgisi.

**Ölçüm önce (ucuz ve kesin):** `chess perft` saf bir movegen benchmark'ıdır (arama/eval
yok). Aşama 1 yazılıp `perft 6` süresi karşılaştırılır -> hem doğruluk (referans
düğüm sayıları birebir) hem hız kazancı **tek koşuda**, search'e hiç dokunmadan.
Ek güvenlik: eski `generate_legal`'ı referans tutup perft yürüyüşünde iki üretecin
küme olarak aynı hamleleri verdiğini iddia eden diferansiyel test (magic vs ray-tracing
doğrulamasının deseni).

**Neden yüksek değerli (öncelik revizyonu):** Zaman-katlama testi bize bir DÖVİZ KURU
verdi — bu motorda arama süresini 2× yapmak ~195 Elo. nps'i %X artırmak süreyi %X
artırmakla aynı. `log2(1+X) × 195`: %25 hız ~ +60 Elo, %40 hız ~ +95 Elo mertebesi;
yani LMP (+34.5) / razoring (+16.5) / cont-hist paketi (+31.6) üstü. ÇEKİNCE: hız
kazancının kendisi henüz TAHMİN (perft ölçecek), döviz kuru 5→10 sn'de ölçüldü ve
uzun TC'de daralır, ±41 hata payı var. Doğru okuma: "onlarca Elo mertebesi", söz değil.
NOT: Daha önce "nps mikro-optimizasyonları düşük öncelik" yazılmıştı — o çıkarım TERSTİ,
düzeltildi. Bu iş HER İKİ TABANA da yarar (klasikte yapılır, NNUE'ya cherry-pick edilir).

**Faz 2D — Multi-threading (Lazy SMP)**

Modern motorların standart, en sade etkili paralel yaklaşımı: N thread aynı kök
pozisyonu, paylaşılan tek bir TT üzerinden arar; TT ve küçük sıralama farklarıyla
doğal olarak ıraksarlar, ana thread raporlar. Gereksinimler:

- [ ] **Thread-safe TT**: ya lockless XOR-key hilesi (bozuk okumayı yakalar) ya da
      per-entry atomik. Yarış koşullarına dayanıklı yapı.
- [ ] **Thread pool + per-thread Searcher durumu** (killer/history/stack/node
      sayacı thread'e özel; TT paylaşılır).
- [ ] **`Threads` UCI option** (spin, min 1, max donanım). Şu an bilinçli ertelenmiş
      durumda — burada hayata geçer.
- [ ] **Determinizm kaybı** kabul edilir: çok-thread arama tekrarlanabilir değildir,
      bu yüzden düğüm sayısı iddiası olan testler tek-thread koşar.
- [ ] **SPRT ile ölçekleme doğrulama**: 1→2→4→8 thread gerçekten Elo veriyor mu?
      (nps ölçeklemesi ≠ Elo ölçeklemesi; ölçülür, varsayılmaz.)

### Faz 3 — NNUE'ya Geçiş

- [ ] Self-play ile pozisyon + sonuç verisi toplama pipeline'ı (ayrı bir
      Python/PyTorch alt-projesi olabilir)
- [ ] HalfKP benzeri feature set tasarımı
- [ ] Küçük feedforward network mimarisi ve eğitim scripti
- [ ] Eğitilen ağırlıkların quantize edilip (int8/int16) motora entegrasyonu
- [ ] Incremental update (accumulator) mantığı
- [ ] SIMD optimizasyonları (AVX2/AVX512) inference hızlandırma için

Bu faza şimdilik detaylı girmiyoruz, Faz 1-2 sağlam oturmadan başlamayacağız.

### Şu an nerede olduğumuzu takip et

Her oturum başında bana hangi fazda, hangi adımda olduğumuzu hatırlat. Eğer bir
önceki oturumdan kalan yarım iş varsa (örneğin test yazılmamış bir fonksiyon,
geçmeyen bir perft testi) önce onu bitirmeden yeni özelliğe geçme.

**Güncel durum (2026-07-10): FAZ 1 + FAZ 2A + FAZ 2B TAMAM, FAZ 2C + 2C-ek + 2C-hız
(Aşama 1/1b) bitti. SIRADAKİ: FAZ 2D (Lazy SMP).** Yeni baseline `127d4f0`
(pin-aware legallik + gürültülü üreteç, SPRT +126.8 Elo). Proje fork'landı: NNUE işi
`../ChessEngineNNUE`'da; bu iki commit oraya cherry-pick edilecek.
Motor UCI üzerinden GUI'ye bağlanıyor, legal oynuyor, perft geçiyor. Toplam 105
test geçiyor. Faz 2B (gelişmiş evaluation + SPRT/maç altyapısı) tamamlandı; tüm
eval terimleri SPRT'den geçti. Faz 2C selective search: PVS + null move + SEE +
LMR + futility ailesi + LMP + razoring TAMAM (hepsi SPRT'den geçti). Check extension
(naif + SEE-kapılı) DENENDİ ama SPRT'de NÖTR -> rafa kaldırıldı. Move ordering
güçlendirme: **history malus SPRT +22.2 Elo kabul**, ardından **continuation history +
tablo kalıcılığı paketi SPRT +31.6 ± 13.8 Elo, LLR 2.96 tam kabul**, ardından
**history-tabanlı LMR + ölçek çarpanı SPRT +13.6 ± 8.5, LLR 2.95 tam kabul (3928 oyun)**.
**FAZ 2C-ek KAPANDI. Yeni baseline `9bdcef4`.** Countermove (regresyon) denendi, geri
alındı. **SIRADAKİ İŞLER (fork sonrası, klasik tabanda): (1) pin-aware legal üretim
[Faz 2C-hız, yüksek beklenen değer], (2) Faz 2D Lazy SMP.** Proje iki repoya ayrılıyor
(klasik + NNUE, ikisi de aktif; bkz. memory `iki-taban-karari`). Ayrıntılı adım-adım
kayıt ve en güncel özet aşağıdaki bölümlerde + memory `proje-durumu`.

**Eval tavan mı? ÖLÇÜLDÜ, hayır (2026-07-10).** Move ordering kazançları küçülünce
"eval yeterince iyi değil mi?" sorusu soruldu. Teşhis: zaman-katlama testi
(`tools/sprt/tc-scaling.ps1`, aynı binary kendine karşı, bir tarafa 2× zaman).
`420d22d`, 5+0.05 vs 10+0.1, 226 oyun: **Elo +195 ± 41, LOS %100**, sıfır zaman aşımı.
Referans band (+50-70) olgun motorların uzun TC değeri; genç motorda + hızlı TC'de
daha büyük olması normal. Sonuç: arama tarafı DOYMAMIŞ, oradaki yatırım hâlâ
karşılığını veriyor. Eval ağırlıklarının hiç tune edilmemiş olması (her katsayı elle
seçilmiş `constexpr`; SPRT'ler yalnız "terim var olmalı mı?" sorusunu cevapladı,
"katsayısı doğru mu?" sorusunu değil) gerçek bir eksik ve ileride +50-100 Elo'luk
kaldıraç, ama ACİL DEĞİL. Faz 3 (NNUE) eval'i zaten değiştirecek.

Faz 1 (tamam):
- Adım 1: CMake + C++20 iskeleti, bitboard `Board` (LERF, çift temsil), UTF-8
  tahta yazdırma.
- Adım 2: Tam move generation. Non-sliding constexpr tablolar; sliding taşlar
  magic bitboard'larla (ray-tracing referansa karşı doğrulandı). Move (16-bit),
  do_move (copy-make), set_fen, legal üretim (pseudo + şah-güvenliği filtresi).
  Perft 6 standart pozisyon geçiyor; startpos d6=119060324 (~68M nps).
- Adım 3: Materyal + PST (Michniewski) evaluation.
- Adım 4: negamax + alpha-beta; mat/pat, 50-hamle beraberliği.
- Adım 5: UCI (uci/isready/ucinewgame/position/go/quit). `chess` varsayılan UCI,
  `chess perft <d> [fen]`, `chess demo`.

Faz 2 (devam ediyor):
- Adım 1: Zobrist hashing + transposition table (TAMAM).
  - `zobrist.hpp/cpp`: sabit tohumlu splitmix64 anahtar tabloları (psq/castling/
    ep/side). `Board.key` + `compute_key()`. put_piece/remove_piece taş-kare
    anahtarını, do_move durum deltalarını (rok/ep/sıra) artımlı günceller.
    İnvaryant testi: her do_move sonrası key == compute_key() (perft yürüyüşü).
  - `tt.hpp/cpp`: `TranspositionTable` (16 MB, 2'nin kuvveti, mask index,
    depth-preferred + yaş değiştirme, EXACT/LOWER/UPPER). search TT sonda+kesme
    (kökte kesme yok), TT hamlesi ile move ordering, sonucu saklar. Mat skoru
    ply-normalize. 50-hamle TT'den önce. Ölçüm: startpos d6 soğuk 1.409.591
    düğüm -> sıcak TT 15.483. Bilinen sınır: halfmove_clock anahtarda yok
    (küçük TT hatası, kabul); TT kesmesi PV'yi kısaltabiliyor (kozmetik).
- Adım 2: Move ordering (TAMAM). `search.cpp` Searcher'a killer moves
  (`killers[ply][2]`) + history (`history[renk][from][to]`) eklendi. `score_move`:
  TT hamlesi > MVV-LVA yakalama/promosyon > killer > history (ayrık bantlar).
  Lazy selection sort ile her iterasyonda en yüksek skorlu hamle öne çekilir.
  Beta kesmesi yapan quiet hamle killer+history'ye işlenir (depth^2 ödül).
  Ölçüm: startpos d6 1.409.591 -> 84.909 düğüm (~16.6× az), skor/PV birebir
  aynı; d8 artık ~1s. Not: killer/history her search() çağrısında (yani her
  derinlikte) sıfırlanıyor — cross-depth kalıcılık ileride search() API'si
  yeniden düzenlenince gelebilir (TT hamlesi zaten derinlikler arası taşınıyor).
- Adım 3: Quiescence search (TAMAM). negamax yaprağı `quiescence()` çağırıyor:
  yalnızca yakalama+promosyon MVV-LVA ile aranır, sessizleşince eval döner.
  Stand-pat (sessizde eval alt sınır, beta kesmesi). Çekteyken stand-pat yok,
  tüm kaçışlar aranır (mat/horizon doğruluğu), kaçış yoksa -MATE+ply. ply tavanı
  ile sonlanır. Etki: startpos d8 gürültülü cp -60 -> temiz cp 0; savunmalı
  piyon pozisyonunda motor artık Rxe5?? oynamıyor (bestmove e1d1, cp 305).
  Test: QuiescenceAvoidsLosingCapture. İleride: SEE ile kayıplı yakalama elemesi,
  delta pruning.
- Adım 4: Iterative deepening + time management (TAMAM). ID döngüsü + aspiration
  windows + zaman yönetimi UCI'den `search.cpp`'ye taşındı: yeni
  `search_iterative(board, SearchLimits, InfoCallback)`. `search()` (sabit
  derinlik) testler için aynen duruyor.
  - Searcher artık derinlikler boyunca YAŞIYOR -> killer/history derinlikler arası
    korunuyor (Adım 2'deki "her search() çağrısında sıfırlanıyor" sınırı çözüldü).
  - Aspiration windows: derinlik >2'de önceki puanın etrafında ±25 cp dar pencere;
    fail-low/high'da ilgili sınır genişletilerek yeniden aranır. Sonuç birebir
    aynı (exact), yalnızca hız. Ölçüm: startpos d11 250.7M/150s -> 221.4M/120s
    (−12% düğüm, −20% süre); Kiwipete d9 9.40M -> 9.18M. Derinlik arttıkça kazanç
    büyüyor. Test: IterativeMatchesFixedDepth (sabit-derinlik ile aynı best/skor).
  - Abort'ta best-move koruma: bir derinlik süre dolunca yarıda kesilirse, kökte
    önceki derinliğe göre gerçek iyileşme bulunmuşsa o hamle korunur (aksi halde
    önceki derinliğin hamlesi). Kök izleme negamax ply==0'da anlık kaydediliyor.
    PV tek hamleye kısaltılır (yarım varyant güvenilmez). Derinlik 1 daima süresiz
    koşar (bestmove 0000 regresyonuna karşı). Test: IterativeReturnsMoveUnderTightTime.
  - Zaman yönetimi (uci.cpp handle_go): movestogo parse edilir, kOverheadMs=30 ms
    güvenlik payı. soft_ms = hedef pay (movestogo>0 -> t/(movestogo+1)+inc/2, yoksa
    t/30+inc/2), hard_ms = min(pay*3, t−overhead). soft: yeni derinliğe başlama
    eşiği; hard: mevcut derinliği bitirmek için mutlak tavan (abort mid-node).
    Doğrulama: Kiwipete movetime 2000'de d8'i 1069ms'de bitirip d9'u başlattı,
    hard limitte kesip d8 hamlesini döndürdü (blunder yok).
  - Asenkron "stop" + gerçek "go infinite": arama ayrı bir std::thread'de koşuyor,
    ana döngü stdin okumaya devam ediyor. `SearchLimits.stop` (atomic<bool>*)
    negamax/quiescence tarafından deadline'dan bağımsız yoklanıyor. "stop"/"position"/
    "ucinewgame"/EOF aramayı keser; "go infinite" zaman sınırsız, yalnızca "stop"la
    (ya da derinlik tavanıyla) durur -> GUI analizinde motor sen durdurana kadar
    düşünür (eski ~3sn'de durma bug'ı çözüldü). Çıktı (info/bestmove/readyok) tek
    g_io_mtx kilidiyle korunuyor. "quit" sınırlı aramayı (depth/movetime/timed)
    doğal bitişine bırakır (pipe/batch tam çıktı), yalnızca sınırsızı zorla durdurur.
    Derinlik 1 daima kesintisiz (deadline+stop devre dışı) -> en az bir hamle garanti.
    Test: InfiniteStopReturnsBestmove. Toplam 55 test.

- UCI seçenekleri (TAMAM). `uci` el sıkışmasında `option name Hash type spin
  default 16 min 1 max 1024` ve `option name Clear Hash type button` ilan edilir;
  `uci.cpp`'ye `handle_setoption` eklendi. `setoption name Hash value N` -> önce
  `stop_search()` sonra `TT.resize(clamp(N,1,1024))` (mevcut resize/clear altyapısı
  kullanıldı, yeni depolama yok). `setoption name Clear Hash` -> `stop_search()` +
  `TT.clear()`. Kanonik UCI isimleri (Arena/Cutechess Hash arayüzünü tanısın diye
  "HashSize" değil "Hash"). Bozuk/eksik value sessizce yok sayılır. Test:
  AdvertisesOptions, SetHashResizesTT, ClearHashHandled, MalformedSetoptionIgnored.
  Toplam 59 test. Not: `Threads` (Lazy SMP) bilinçli ertelendi — gerçek güç
  özelliği, kendi adımında SPRT ile yapılacak; çalışmayan option ilan edilmedi.

Faz 2B (devam ediyor):
- Adım 1: Tapered eval altyapısı (TAMAM). `eval.hpp`/`eval.cpp` orta oyun (MG) /
  oyun sonu (EG) çift-tablo + oyun fazına göre interpolasyona geçirildi.
  - `PhaseWeight` (N=1,B=1,R=2,Q=4; piyon/şah=0), `MAX_PHASE=24` (tam kadro).
    `game_phase(b)` = kalan taşların ağırlık toplamı, tavanla sınırlı.
  - `PstMg`/`PstEg`: EG tablosu şu an yalnızca ŞAH'ta farklı (orta oyun rok
    bölgesi güvenli, oyun sonu merkez aktif — Michniewski EG şah tablosu); diğer
    taşlar ve materyal her iki fazda eşit. `to_lerf()` ham görsel tabloyu LERF'e
    çevirir. `evaluate()` mg/eg ayrı biriktirir: score=(mg*phase+eg*(24-phase))/24.
  - Testler: PhaseFullBoardIsMax(=24), PhaseBareKingsIsZero(=0),
    PhaseDropsWhenQueensLeave, KingCentralizedInEndgame (faz 0'da merkez şah >
    köşe şah). WinsHangingQueen/MoveOrderingPreservesTacticAtDepth eşiği >500 ->
    >400 (Rxe5 hâlâ doğru; K+R vs K oyun sonunda aktif siyah şah EG payı skoru
    479'a çekiyor — doğru davranış). Toplam 63 test.
- Adım 2: `Board::to_fen()` + `chess fen [uci-moves...]` modu (TAMAM). set_fen'in
  tersi; round-trip garantili (en passant/rok/sayaç doğru serileştirilir). "chess
  fen" başlangıçtan hamleleri legal oynayıp FEN basar — açılış kitabını elle FEN
  yazmadan üretmek için (Faz 3 self-play pipeline'ı da kullanacak). Testler:
  StartposString, RoundTrip (Kiwipete/ep/kısmi rok), EnPassantAfterDoublePush.
- Adım 3: SPRT maç/regresyon altyapısı (TAMAM, cutechess-cli KURULU).
  `tools/sprt/`: build-version.ps1 (git ref -> /O2 Release chess-<label>.exe,
  worktree izole, build dizini gitignore'lu build-release altında), gen-book.ps1
  + book.epd (22 dengeli açılış, motorun `chess fen`'iyle doğru üretildi),
  sprt.ps1 (cutechess-cli sarmalayıcı: -repeat çift renk, -sprt otomatik durma,
  elo0/elo1/alpha/beta parametrik + opsiyonel -Hash), README.md (kurulum + iş
  akışı + karar yorumu). CMakeLists `BUILD_TESTS` opsiyonu (OFF -> gtest
  indirmesi atlanır, sürüm hızlı). **cutechess-cli artık bu makinede KURULU**
  (`C:\Program Files (x86)\Cute Chess\cutechess-cli.exe`; sprt.ps1 ve GUI
  otomatik buluyor). İlk planlı koşu: tapered eval'in (bbbe48b vs d7e6754) Elo
  katkısı. Toplam 66 test.
- Adım 4: SPRT komutsuz web arayüzü (TAMAM). `tools/sprt/gui/`: üç scripti
  (build-version + gen-book + sprt) elle sırayla çağırmak yerine tarayıcıdan
  yönetmek için yerel web UI. `start-gui.cmd`'ye çift tıkla -> `serve.ps1`
  (PowerShell 5.1 gömülü .NET TcpListener HTTP sunucu, ek kurulum/admin yok;
  API: /api/status,commits,run,progress,stop,open,saveconfig) + tarayıcı otomatik
  açılır. `index.html` tek sayfa (inline CSS/JS): Base/New commit seçimi, izlek
  (concurrency; varsayılan fiziksel çekirdek, üstünde uyarı), Tc+preset,
  Elo0/Elo1+preset, alpha/beta, Hash, rounds, force-rebuild. Canlı panel:
  W-D-L, Elo±hata, LOS, SPRT LLR + karar, Durdur, PGN aç. `run-pipeline.ps1`
  base derle -> new derle (çalışan ağaç daima yeniden) -> sprt.ps1, alt-process
  stdout'unu satır satır log'a akıtır (saatlerce süren maçta canlı akış).
  Uçtan uca test edildi. Donanım notu: CPU 14 fiziksel / 20 mantıksal çekirdek.
- Adım 5: Tapered eval SPRT baseline (TAMAM). Metodoloji borcu kapatıldı:
  d7e6754 (tapered öncesi) vs bbbe48b (tapered), GUI'den. 709 oyun, W-D-L
  189-418-102, **Elo +42.8 ± 16.3, LLR 2.97, H1 kabul** — tapered eval'in Elo
  katkısı kanıtlandı.
- Adım 6: Pawn structure (TAMAM, commit 067f822). evaluate()'e ilk gelişmiş eval
  terimi: izole + çift + geçer piyon, renk-simetrik, tapered (MG/EG ayrı, tek
  taper sonda). `eval.hpp`: file/adjacent/passed constexpr maskeleri (detail,
  `to_lerf` deseni) + ağırlıklar (izole -12/-15, çift -10/-20 [EG'de ağır],
  geçer piyon sıra-bağlı bonus tablosu [EG belirgin büyük]) + `pawn_structure(b,
  mg,eg)` (beyaz−siyah, PST gürültüsü olmadan izole test edilebilir). `eval.cpp`:
  çift = sütun başına (çift-sayım yok), izole+geçer = piyon başına. 4 yeni test
  (66 -> 70). Elle kontrol: geçer-piyon pozisyonunda +235cp, motor piyonu şahıyla
  eskort ediyor. **SPRT: base 26c833d vs new 067f822, 785 oyun, W-D-L 239-409-137,
  Elo +45.4 ± 16.8, LLR 2.97, H1 kabul** — terim kanıtlandı, tutuldu. Bilinçli
  ertelenen: pawn hash table (hız), ince ağırlık tuning'i.
  - Sıradaki 2B işleri: king safety, piece mobility, bishop pair, rook on
    open/semi-open file — her biri ayrı commit + ayrı SPRT'den geçirilerek
    (altyapı + GUI hazır).
- Ek (arama doğruluk düzeltmesi): Tekrar (repetition) tespiti (TAMAM, commit
  3d04898). Kullanıcı bildirimi: motor kazandığı pozisyonda bile 3-hamle
  tekrarıyla berabere yapıyordu (arama yalnız mat/pat + 50-hamle görüyordu).
  Çözüm: oyun pozisyon geçmişi (Zobrist anahtarları) UCI `handle_position`'da
  toplanıp search'e `history` parametresiyle taşınıyor; negamax'ta ply>0'da TT
  sondasından ÖNCE `is_repetition` (halfmove_clock sınırlı, aynı-sıra pariteli
  step-2 tarama, ilk eşleşme = beraberlik 0); RAII `KeyGuard` ile her düğümün
  anahtarı push/pop (abort dahil her yolda dengeli). Önde olan taraf tekrarı
  reddeder, geride olan (perpetual) arar. Testler: ForcedRepetitionSavesLosingSide
  (ayırt edici: geçmişle 0, geçmişsiz -900), WinningSideAvoidsRepetition (70->72).
  E2e: KQvK sallanma geçmişiyle motor mate-in-7 buluyor. **SPRT: base 02c12f5 vs
  new 3d04898, 1534 oyun, W-D-L 468-718-348, Elo +27.2 ± 12.7, LLR 2.97, H1 kabul.**
  Bilinçli ertelenen: contempt (draw≠0), cuckoo-tablo hızlı tespiti.
- Adım 7: Piece mobility (TAMAM, commit 07060b1). İkinci gelişmiş eval terimi:
  at/fil/kale/vezir için ulaşılabilir (dost olmayan) kare sayısı × tür ağırlığı,
  renk-simetrik, tapered. `eval.hpp`: MobilityMg/Eg[tür] (at/fil 4/4, kale 2/4
  [EG'de açık hat], vezir 1/1 [erken çıkış cezası]) + `mobility(b,mg,eg)` ilanı.
  `eval.cpp`: attacks.hpp include, mevcut knight/bishop/rook/queen_attacks
  tablolarıyla, evaluate() akümülatöre ekliyor. 2 test (72->74). Elle: startpos
  +11cp/e2e4, açıkta Bb5. Tam kadro nps ~1.84M->1.33M (~%28 maliyet). **SPRT:
  base 3d04898 vs new 07060b1, H1 kabul** — terim tutuldu. Bilinçli ertelenen:
  mobility area rafinesi (rakip piyon vuruşlarını çıkarma), mobility cache/hız.
- Adım 8: Bishop pair + rook on open/semi-open file (TAMAM, commit 9abe61b). İki
  klasik pozisyonel terim tek yamada, renk-simetrik, tapered. `eval.hpp`:
  BishopPairMg/Eg (30/45), RookOpenMg/Eg (25/15), RookSemiMg/Eg (12/8) +
  `bishop_pair` / `rook_on_file` ilanları. `eval.cpp`: bishop_pair (>=2 fil ->
  bonus), rook_on_file (mevcut FileMask; dost piyon yok + rakip piyon yok ->
  açık, yalnız rakip -> yarı-açık). 4 test (74->78). Elle: açık c/d sütunlu
  pozisyonda motor Rac1/Rhd1 (kaleyi açık sütuna). **SPRT: base 07060b1 vs new
  9abe61b, H1 kabul** — tutuldu. Bilinçli ertelenen: bishop pair zıt-kare
  kontrolü, 7. sıra / bağlı kale rafineleri.
- Adım 9: King safety (TAMAM, ERKEN KABUL — commit 7eea85f). Faz 2B'nin son gelişmiş
  eval terimi, diğerleriyle aynı desende (renk-simetrik, tapered, izole test
  edilebilir yardımcı). **Yalnız orta oyun terimi (eg her zaman 0)** — oyun
  sonunda şah aktifliği önemli, güvenlik taper ile solar (KingCentralizedInEndgame
  korunur). `eval.hpp`: (a) piyon kalkanı — şahın önündeki iki sıra, kf-1..kf+1
  sütunlarında dost piyon yoksa sütun başına ShieldMissingPenalty(15) "danger";
  (b) şah bölgesi (king ring = king_attacks(ksq)) saldırıları — rakip taşların
  halkada vurduğu kare sayısı × KingAttackWeight (at/fil 2, kale 3, vezir 5) =
  attack units, doğrusal olmayan SafetyTable[units] (CPW "King Safety" standart
  tablosu; düşük baskıda ~0 -> normal pozisyonlar cezalanmaz, gerçek saldırıda
  hızla büyür, 500'de doygun). `eval.cpp`: `king_safety(b,mg,eg)` her renk için
  danger toplar, `mg += -sign*danger` (tehlike o rengin skorunu düşürür), evaluate()
  akümülatöre ekler. 3 test (78->81): KingSafetyPawnShield, KingSafetyZoneAttack,
  KingSafetySymmetry. Startpos d10 nps ~1.32M (mobility sonrasıyla ~aynı, Debug).
  **SPRT: base 9abe61b vs new 7eea85f — tam sınıra ulaşmadan 1000 oyunda KULLANICI
  KARARIYLA ERKEN KABUL** (zamandan kazanmak için). Kanıt güçlü pozitifti: Elo
  +28.6 ± 18.6, LOS %99.9, LLR 1.46 (üst sınır 2.94'ün ~yarısı, yükseliyordu),
  W-D-L 411-260-329. NOT: diğer beş eval terimi tam LLR ≥ 2.94 ile kabul edildi;
  bu terim metodolojik olarak onlardan farklı (erken durdurma) — istatistiksel
  kesinlik değil, güçlü eğilim + kullanıcı kararı. Bilinçli ertelenen:
  queen-presence gate, king_safety'yi mobility ile tek saldırı geçişinde
  birleştirme (hız), SafetyTable scaling ince ayarı.

Faz 2C (selective search — devam ediyor):
- Adım 1: PVS (Principal Variation Search) (TAMAM). negamax
  hamle döngüsünde: ilk (en iyi sıralanan) hamle tam pencere; kalanlar null-window
  scout [-alpha-1, -alpha]; scout alpha'yı geçip beta'nın altında kalırsa tam
  pencereyle yeniden aranır. Selection sort zaten en iyi hamleyi i==0'a getirdiği
  için ilk-hamle varsayımı sağlanıyor; abort hem scout hem re-search sonrası
  yoklanıyor; fail-soft korunuyor. **PVS exact (davranış-koruyan) optimizasyon ->
  SPRT YOK; kapı exactness** (kullanıcı kararı; SPRT LMR'ye saklandı). Doğrulama
  (Debug, PVS öncesi 7eea85f vs sonrası): startpos d9 cp34 e2e4 birebir (nodes
  10.46M->10.25M, −2%), Kiwipete d8 cp5 e2a6 birebir (13.08M->12.71M, −2.8%),
  startpos d10 cp1 b1c3 (47.36M->44.04M, −7%). Score+bestmove üçünde birebir aynı;
  d10'da PV 4. hamleden sonra ayrışıyor ama bu bug değil — PVS PV düğümlerini tam
  pencereyle aradığından pv_table daha dolu doluyor (eski TT-kesmeli 6-hamle PV ->
  10-hamle dolu PV), score+bestmove değişmiyor. Düğüm düşüşü derinlikle büyüyor
  (tipik PVS). 81 test geçiyor (IterativeMatchesFixedDepth PVS iç tutarlılığını
  yakalar). Not: PVS'in asıl değeri LMR/null-move'un oturacağı null-window çerçevesi.
- Adım 2: Null move pruning (TAMAM, SPRT GEÇTİ H1). Sıradaki tarafa "bedava
  hamle" (pass) verilir; oluşan pozisyon azaltılmış derinlikte beta etrafında
  null-window ile aranır, skor >= beta ise dal budanır. Sezgisel (heuristic)
  budama — davranış-koruyan DEĞİL, kabul kapısı SPRT (Elo), düğüm düşüşü değil.
  - `board.hpp/cpp`: `make_null_move()` (do_move ep+side Zobrist deseniyle
    simetrik: ep hakkını düşür, sırayı çevir, halfmove_clock++, key artımlı) +
    `has_non_pawn_material(c)` (zugzwang koruması: `colors[c] & ~pawn & ~king`).
  - `search.cpp`: negamax'a `bool null_allowed = true` parametresi (mevcut çağrı
    yerleri değişmeden derlenir; yalnız null-child'a false geçilir -> üst üste iki
    null yasak). TT sonda + KeyGuard sonrası, move döngüsünden önce null bloğu.
    Koşullar: null_allowed && ply>0 (kökte asla) && depth>=3 && !is_mate_score(beta)
    (beta=INF PV-ilk hamlesini de dışlar) && has_non_pawn_material(us) && çekte
    değil. R=(depth>=6)?3:2, `[-beta,-beta+1]` null-window, score>=beta -> return
    beta (fail-hard; TT'ye yazmaz). copy-make (`Board next=b; next.make_null_move()`).
  - Doğal güvenlik: kökte null yok (PV/bestmove bozulmaz); PVS'te PV-ilk hamlesi
    beta=INF -> is_mate_score true -> o düğümde null denenmez.
  - Testler (81->84): BoardNullMove.FlipsSideAndKeepsKey (key==compute_key, ep
    düştü, side çevrildi), HasNonPawnMaterial, Search.NullMoveKeepsZugzwangCorrectness
    (K+P endgame'de guard null'ı kapatır, kazanç korunur). Tüm eski testler geçiyor.
  - Düğüm sağlaması (Debug, PVS baseline'a karşı): startpos d10 44.04M -> 3.90M
    (~11× az), skor cp1 aynı, bestmove b1c3; Kiwipete d10 26.35M, bestmove e2a6
    (PVS ile aynı — taktik pozisyon daha az budanıyor, beklenen). Bu bir sağlama,
    kapı değil. Bilinçli ertelenen: verification search (yüksek derinlik zugzwang
    doğrulaması), statik eval>=beta gate (reverse-futility ile birleşecek), R ince
    ayarı. **SPRT: base 1883432 (PVS) vs new 0dfac0b, 1021 oyun, W-D-L 476-233-312,
  Elo +56.3 ± 18.9, LOS %100, LLR 2.95 (tam kabul, erken durdurma değil), H1
  kabul** — Faz 2C'nin şu ana dek en büyük tekil kazancı, tutuldu.
- Adım 3: SEE — Static Exchange Evaluation (TAMAM, SPRT GEÇTİ H1). Bir
  karedeki taş alışverişinin materyal sonucunu STATİK hesaplar (swap algoritması,
  CPW). Yol haritası kullanımı üçe ayrılıyor: (a) qsearch'te kayıplı yakalamaları
  ele [BU ADIM], (b) delta pruning [ertelendi], (c) LMR/futility [sonraki adımlar].
  - YENİ `see.hpp/cpp`: `int see(const Board&, Move)` — hamle sırası tarafının
    bakışıyla net materyal (santipiyon). Ön koşul: m bir yakalama (NORMAL/EN_PASSANT;
    promosyon verilmez). Yardımcılar: `attackers_to(b,to,occ)` (is_square_attacked
    deseninin tam-küme biçimi, iki renk), `see_value(pt)` (MaterialValue; şah=10000
    -> feda edilmez). Swap: gain[0]=captured; her adım gain[d]=see_value(aPiece)-
    gain[d-1], güvenli budama, kullanılan saldıranı occ'tan çıkar, attadef=
    attackers_to&occ YENİDEN hesapla (x-ray + kaldırılanlar occ maskesiyle otomatik),
    en ucuz saldıranı seç (PAWN..KING); sonda minimax geri-katlama. Ep: alınan piyon
    occ'tan başta çıkar. Sezgisel (pin/çek görmez) -> kabul kapısı SPRT.
  - `search.cpp` quiescence: çekte değilken, promosyon olmayan yakalamalardan
    yalnız `see(b,m) >= 0` aranır (kayıplı yakalamalar elenir). Çekte hepsi,
    promosyon daima aranır (SEE muaf).
  - Testler (84->89): See.WinningCaptureFreePiece (savunmasız vezir=900),
    EqualTradePawnDefended (=0), LosingCaptureRookForPawn (RxP savunulu=-400),
    XrayStackedRooks (istiflenmiş kale zinciri=-400), EnPassantWinsPawn (=100).
    Tüm eski testler geçiyor.
  - Düğüm sağlaması (Debug, null-move baseline'a karşı): startpos d10 (sessiz)
    3.90M->4.11M (+%5, budanacak kayıplı yakalama yok, see çağrı maliyeti); Kiwipete
    d10 (taktik) 26.35M->16.38M (−%38), skor/bestmove/PV birebir aynı (cp-10, e2a6).
    Bu bir sağlama, kapı değil. Bilinçli ertelenen: delta pruning, SEE'nin LMR/
    futility'de kullanımı, promosyon SEE, incremental x-ray, SEE move-ordering.
    **SPRT: base 0dfac0b (null move) vs new b3f7586, 2016 oyun, W-D-L 802-573-641,
    Elo +27.8 ± 12.8, LOS %100, LLR 2.95 (tam kabul), H1 kabul** — tutuldu.
- Adım 4: LMR (Late Move Reductions) (TAMAM — kod + testler; SPRT SIRADA).
  Klasik fazın en büyük tekil Elo kazancı beklenen teknik. İyi move ordering'de
  geç sıralanan quiet hamleler büyük olasılıkla alpha'yı geçmez -> azaltılmış
  derinlikte (null-window scout) aranır; beklenmedik şekilde alpha'yı geçerse
  indirim yanlıştı -> tam derinlikte yeniden aranır (re-search kayıp taktiği
  kurtarır). Heuristik (davranış-koruyan DEĞİL) -> kabul kapısı SPRT (Elo).
  - `search.cpp`: (a) namespace `lmr_reduction(depth,move_num)` — bir kez
    hesaplanan log-tabanlı `int8_t[64][64]` tablo, r = 0.75 + ln(d)*ln(m)/2.25
    (CPW/Stockfish tarzı; bölen/taban SPRT ile ayarlanabilir). `<array>`+`<cmath>`.
    (b) `in_check` + `us` move üretiminden sonra bir kez hesaplanıp hoist edildi
    (null-move koşulu + LMR paylaşıyor, per-move maliyet yok). (c) PVS scout `else`
    dalı LMR ile sarıldı: reduction hesapla -> `depth-1-reduction` null-window scout
    -> scout alpha'yı geçtiyse `depth-1` null-window re-search -> PVS tam-pencere
    re-search. Abort yoklamaları + fail-soft korundu.
  - İndirim uygulanmaz (gate): ilk iki hamle (i<2, PV+ilk scout), depth<3,
    yakalama/promosyon, çekteyken (kaçış), çek veren hamle, killer'lar. reduced
    derinlik en az 1 ply (reduction <= depth-2).
  - Testler (89->91): LmrPreservesTacticAtDeepDepth (bedava vezir d8'de hâlâ Rxe5),
    LmrFindsMateUnderReductions (mat-in-1 d7'de korunur). Tüm 91 test geçiyor.
  - Düğüm sağlaması (Release, SEE baseline b3f7586'ya karşı, d10): startpos
    4.11M->253K (~16× az), best b1c3 korundu (skor cp1->cp17, heuristik kayması
    beklenen); Kiwipete 16.38M->1.24M (~13× az), best e2a6 korundu (cp-10->cp-2).
    Nominal derinlik aynı düğüm bütçesinde çok daha derine iniyor. Bu bir sağlama,
    kapı değil. Bilinçli ertelenen: PV-node/improving farkı, history-tabanlı
    indirim, depth-bağımlı gate (LMP ile birleşir), bölen/taban ince tuning.
  - **SPRT GEÇTİ: base b3f7586 (SEE) vs new fa57661 (LMR), 313 oyun, W-D-L
    184-83-46, Elo +164.5 ± 35.3, LOS %100, LLR 2.95 (tam kabul), H1 kabul** —
    yol haritasının öngördüğü gibi klasik fazın AÇIK ARA en büyük tekil kazancı
    (null move +56.3'ün ~3 katı), tutuldu.
- Adım 5: Futility ailesi — RFP + futility pruning (TAMAM, SPRT GEÇTİ H1 —
  commit 7254fa6). Yol haritasının "Futility + reverse futility (static null
  move)" maddesi, tek commit (kullanıcı kararı: yol haritası tek madde olarak
  grupluyor). İki tamamlayıcı near-leaf heuristik, düğüm seviyesinde bir kez
  hesaplanan `static_eval`'i paylaşır — evaluate() artık interior düğümde de
  çağrılıyor (önce yalnız quiescence yaprağındaydı). Sezgisel (davranış-koruyan
  DEĞİL) -> kabul kapısı SPRT (Elo).
  - `search.cpp` negamax: (a) `in_check` sonrası `static_eval = in_check ? 0 :
    evaluate(b)` (çekteyken kullanılmaz). (b) RFP (reverse futility / static null
    move) — null move bloğundan önce: `!in_check && ply>0 && depth<=kRfpMaxDepth
    (6) && !is_mate_score(beta) && static_eval - kRfpMargin(80)*depth >= beta` ->
    `return static_eval` (fail-soft; static_eval>=beta olduğundan geçerli fail-high;
    TT'ye yazmaz, null move deseni). (c) Futility pruning — node-level `can_futility`
    bayrağı (`!in_check && ply>0 && depth<=kFutilityMaxDepth (3) && !is_mate_score
    (alpha) && static_eval + kFutilityMargin[depth] ({0,150,250,400}) <= alpha`);
    döngüde çek vermeyen quiet hamleler `moves_searched>0` guard'ıyla aranmadan
    atlanır (`continue`). (d) Döngü refaktörü: `quiet`/`gives_check` do_move sonrası
    bir kez hesaplanıp futility + LMR paylaşır (çift hesap yok); `moves_searched`
    sayacı. i==0 (PVS ilk hamle) daima aranır -> fail-low düğümde bile bir hamle/PV
    kalır.
  - Doğal güvenlik: ply>0 (kök tam aranır), !is_mate_score (mat penceresinde
    kapalı, beta/alpha=±INF dahil -> PV düğümlerinde budama yok), !gives_check +
    quiet (taktik/çek hatları korunur), moves_searched>0 (en az bir hamle garanti).
  - Testler (91->93): FutilityKeepsWinningTactic (bedava vezir d4'te hâlâ Rxe5 —
    yakalama quiet değil, budanmaz), RfpKeepsMateSearch (mat-in-1 d5'te korunur).
    Düğüm sağlaması (Debug, LMR baseline'a karşı, d10): startpos 253K->176K (−30%,
    best d2d4), Kiwipete 1.24M->340K (−73%, best e2a6 + cp-2 birebir). Bu bir
    sağlama, kapı değil. Bilinçli ertelenen: margin/max-depth tuning, null move
    `static_eval>=beta` gate ile birleştirme.
  - **SPRT GEÇTİ: base e6ed399 (LMR) vs new 7254fa6, 313 oyun, W-D-L 164-107-42,
    Elo +143 ± 32.4, LOS %100, LLR 2.95 (tam kabul), H1 kabul** — LMR'den sonra
    klasik fazın İKİNCİ en büyük tekil kazancı, tutuldu.
- Adım 6: LMP (Late Move Pruning / move-count) (TAMAM, SPRT GEÇTİ H1 — commit
  9ea1677). Sığ, çekte-olmayan, mat-olmayan düğümde iyi sıralamada belli sayıdan
  sonraki quiet, çek vermeyen hamleleri statik eval'e bakmaksızın, SADECE sıra
  numarasına göre atlar. Futility'nin (eval-tabanlı) tamamlayıcısı — aynı hamle
  döngüsünde yan yana, paylaşılan `quiet`/`gives_check`/`moves_searched` ile.
  Sezgisel (davranış-koruyan DEĞİL) -> kabul kapısı SPRT (Elo).
  - `search.cpp`: `kLmpMaxDepth (8)` + `lmp_count(depth) = 3 + depth*depth` eşik
    (d1->4, d2->7, d3->12, d4->19; derinde eşik legal hamle sayısını aşıp
    tetiklenmez — asıl budama sığ). Node seviyesi `can_lmp` kapısı (`!in_check &&
    ply>0 && depth<=kLmpMaxDepth && !is_mate_score(alpha) && !is_mate_score(beta)`);
    döngüde futility `continue`'sunun ardına `moves_searched >= lmp_count(depth) &&
    quiet && !gives_check -> continue`.
  - Doğal güvenlik: ply>0 (kök tam), mat penceresinde kapalı, !gives_check + quiet
    (taktik/çek/yakalama korunur), lmp_count>=4 -> en az PV + birkaç hamle daima
    aranır (fail-low düğümde bile hamle/PV kalır).
  - Testler (93->95): LmpKeepsWinningTactic (bedava vezir d4'te hâlâ Rxe5),
    LmpKeepsMateSearch (mat-in-1 d5'te korunur). Düğüm sağlaması (Release, futility
    baseline 7254fa6, d10): startpos 176K->70K (best d2d4 korundu); Kiwipete
    340K->467K + best e2a6->d5e6 (heuristik, davranış-koruyan DEĞİL — tek taktik
    düğümde LMP cutoff-üreten hamleyi budayıp ağacı yeniden şekillendirebilir; bu
    bir sağlama, kapı değil).
  - Metodolojik not: erken kareler (247 oyun) düz görünüyordu (Elo +4.3, LLR 0.03);
    LMP mütevazı bir etki olduğundan LMR/futility gibi 313 oyunda patlamadı, daha
    çok oyun istedi. Sabırla koşu tamamlandı. Bilinçli ertelenen: improving farkı,
    PV/non-PV ayrımı, LMP+LMR eşik ortak tuning'i.
  - **SPRT GEÇTİ: base 7254fa6 (futility) vs new 9ea1677, 1413 oyun, W-D-L
    520-513-380, Elo +34.5 ± 14.5, LOS %100, LLR 2.95 (tam kabul), H1 kabul** —
    tutuldu.
- Adım 7: Razoring (TAMAM, SPRT GEÇTİ H1 — commit 185f712). RFP'nin fail-low
  aynası: sığ düğümde static_eval alpha'nın çok altındaysa, tam-derinlik aramaya
  girmeden qsearch ile doğrula; qsearch de alpha'yı geçemiyorsa dalı fail-low ile
  buda. İkisi (RFP fail-high / razoring fail-low) doğası gereği karşılıklı
  dışlayıcı. Sezgisel (davranış-koruyan DEĞİL) -> kabul kapısı SPRT (Elo).
  - `search.cpp`: `kRazorMaxDepth (3)` + `kRazorMargin[4] ({0,300,500,700})`.
    Razoring bloğu `static_eval` hesabından sonra, RFP'den ÖNCE (static_eval'i
    bedavaya paylaşır, ek eval yok). Koşullar RFP/futility deseninde: `!in_check
    && ply>0 && depth<=kRazorMaxDepth && !is_mate_score(alpha) && static_eval +
    kRazorMargin[depth] <= alpha` -> `quiescence(b,alpha,beta,ply)`; skor <= alpha
    ise `return score` (fail-low). Taktik/yakalama qsearch'te görüldüğünden budanmaz.
  - Doğal güvenlik: ply>0 (kök tam), mat penceresinde kapalı; kökün ilk PV
    hamlesinde alpha=-INF -> `static_eval+margin <= -INF` asla doğru olmaz, PV
    düğümlerinde doğal olarak tetiklenmez.
  - Testler (95->97): RazoringKeepsWinningTactic (bedava vezir d4'te hâlâ Rxe5 —
    yakalama qsearch'te görülür), RazoringKeepsMateSearch (arka sıra matı d5'te
    korunur). Düğüm sağlaması (Release d10, LMP baseline tutarlı): startpos d2d4
    cp16 69.9K (quiet -> razoring nadir tetikleniyor), Kiwipete d5e6 cp37 460K.
    Bu bir sağlama, kapı değil.
  - **SPRT GEÇTİ: base 9ea1677 (LMP) vs new 185f712, 2912 oyun, W-D-L
    902-1246-764, Elo +16.5 ± 9.5, LOS %100, LLR 2.95 (tam kabul), H1 kabul** —
    futility/LMP ile örtüştüğünden mütevazı kazanç, LMP gibi çok oyun istedi ama
    kesin pozitif, tutuldu. Bilinçli ertelenen: null-window qsearch varyantı,
    PV/non-PV ayrımı, depth-quadratic margin (modern Stockfish tarzı).
- Adım 8: Check extension — DENENDİ, RAFA KALDIRILDI (SPRT NÖTR). İki form:
  (a) naif (tüm gives_check hamlelerini 1 ply uzat, commit 87a8f37); (b) SEE-kapılı
  (Stockfish see_ge deseni: yalnız see>=0 çekleri uzat, commit b0e4f54; see()
  sessiz-hamleleri destekleyecek şekilde genelleştirildi). İKİSİ DE SPRT'de NÖTR
  (LLR ~0) -> revert 599050a (davranış == 185f712). Ders: çok-budayan yığında
  çeklerin taktik değeri zaten yakalanıyor. NOT: see() sessiz-hamle genelleştirmesi
  KORUNDU (latent altyapı, ileride quiet-move SEE budaması için; 2 SEE testi).
- Move ordering güçlendirme (Adım 8 sonrası pivot):
  - **History malus (işaretli history) — SPRT +22.2 Elo, ERKEN KABUL (commit
    f12982c).** Beta kesmesinde kesen quiet ödül (+depth²), o düğümde önce boşuna
    aranan quiet'ler ceza (-depth²); history [-700k,+700k] merkezli. update_quiet_stats
    aranmış quiet listesi alır; negamax quiets_searched[64] toplar. Kötü quiet'ler
    negatife düşüp sıralamada sona gider. SPRT base 185f712 vs new f12982c: 804 oyun
    Elo +22.2 ± 18.6, LOS %99, LLR 1.1 yükseliyordu -> kullanıcı erken H1 kabul.
    YENİ BASELINE.
  - Countermove heuristic (commit 875d5f5) — REGRESYON (-60 Elo), GERİ ALINDI
    (487b957). Sabit 780k bant + agresif LMP iyi hamleleri buduyordu. İleride
    history-bonusu olarak denenebilir (sert bant değil).
  - Main-history-tabanlı LMR (commit 4520bc1) — NO-OP (ödül-only) sonra malus
    üstünde OVER-REDUCTION (SPRT ~-44), GERİ ALINDI (89b9f2b). Malus zaten kötü
    hamleleri geç sıralayıp yüksek base indirim veriyor; history-LMR çifte indirince
    aşırı-indirim. O zamanki teşhis: "yalnız main history var, malus ile örtüşüyor".
    GERÇEK KÖK SEBEP sonradan ölçüldü (aşağı bak): tablolar her hamlede sıfırlanıyordu,
    stat sinyali yapısal olarak açtı -> ayar hiç tetiklenmiyordu.
  - **Continuation history + tablo kalıcılığı (PAKET) — SPRT +31.6 ± 13.8 Elo,
    LLR 2.96 TAM KABUL, 1400 oyun (465-597-338, LOS %100). Base 420d22d, new ae20258.
    YENİ BASELINE.** İki commit:
    - `e9821a9` **continuation history**: `cont_hist[önceki taş][önceki hedef][taş][hedef]`
      (~2.36 MB, heap). Searcher'a ply-indeksli arama yığını (`stack[ply]` = o ply'de
      oynanan hamlenin taş+hedef bağlamı; null move'da kNoPiece, kökte yok). score_move
      quiet puanı = main + cont, toplam ±kHistoryMax'a kırpılır (killer bandı korunur).
      update_quiet_stats aynı ödül/ceza şemasını cont'a da işler; ortak kırpma
      `add_history()`. **Tek başına SPRT: 2276 oyun, +6 ± 10.9, LOS %86, LLR 0.567 —
      KARARSIZ** (elo0=0/elo1=5 ile gerçek etki sınıra yapışınca LLR ~0.00025/oyun,
      karar ~12k oyun isterdi). Kesin bilgi: −5 Elo'dan kötü regresyon dışlandı.
    - `ae20258` **tablo kalıcılığı** (asıl kaldıraç). BULGU: `search_iterative` içinde
      `Searcher` yerel değişkendi -> killer/history/cont-hist her `go`'da, yani oyunun
      HER HAMLESİNDE sıfırdan doğuyordu. Ölçüldü (geçici enstrümantasyon): LMR'ye giren
      quiet'lerin |stat| ortalaması startpos movetime 200'de **131**, Kiwipete'de **1**
      (maks 75). Seyrek cont-hist tek hamlelik aramada dolmuyor. Stockfish bu tabloları
      oyun boyunca taşır, yalnız ucinewgame'de temizler. Çözüm: yeni `SearchTables`
      (pimpl, search.hpp) -> sahiplik çağırana; `search_iterative(..., SearchTables*
      tables = nullptr)`; nullptr -> geçici tablolar (testler deterministik).
      `Impl::age()` her arama başında history /= 2 + killer temizliği: yarılama hem
      sert-clamp'li birikimin ±700k tavanına oturup sinyali doyurmasını önler hem eski
      bilgiyi söndürür; killer'lar ply-indeksli olduğundan (kök 2 ply kayar) taşınmaz.
      `Impl::clear()` = ucinewgame. uci.cpp `g_tables` (stop_search() join ettiğinden yarış yok).
      Doğrulama (Release, TT arada Clear Hash ile izole): ucinewgame YOK -> 2. arama d10'da
      55.010 düğüm (taze 87.436, −%37, birikim taşınıyor); ucinewgame VAR -> tam 87.436
      (temizlik doğru). Faz 2D'de her thread kendi SearchTables örneğini alır.
    - DERS: "cont-hist küçük bir yama" sonucu yanıltıcıydı; asıl sorun altyapıdaydı.
      Tek başına +6, kalıcılıkla birlikte +31.6.
  - **History-tabanlı LMR + history ölçek çarpanı — SPRT +13.6 ± 8.5 Elo, LLR 2.95
    TAM KABUL, 3928 oyun (1284-1514-1130, LOS %99.9). Base ae20258 vs new 9bdcef4.
    YENİ BASELINE.** `stat = main + cont` (ham, kırpılmamış); LMR taban indiriminden
    `clamp(stat / kLmrStatDiv, -2, +2)` çıkarılır; mevcut sınırlar (reduction <=
    depth-2, >= 0) korunur. ±2 ply kelepçe, geri alınan `4520bc1`'in yoksun olduğu
    over-reduction freni.
    - ÖLÇÜM ÖNCE, KOD SONRA. İlk deneme (bölen 300k) **no-op** çıktı; düğüm sayılarının
      baseline ile birebir aynı kalması bunu ele verdi (ucuz ve güvenilir dedektör).
    - Ölçüldü (geçici enstrümantasyon, gerçek oyun dizisinde): ölçeksiz (bonus=depth²)
      |stat| ort **3**, maks 165, **%42-71'i TAM SIFIR**, >=100 olan %0.5. Tavan 700k =
      tipik bonusun ~5000 katı. Üstelik `age()`'deki `h /= 2` tamsayı bölmesi 3
      büyüklüğündeki değerleri **siliyordu** (söndürmüyor, yok ediyordu). Hiçbir bölen
      bu dağılıma granülarite veremez -> sorun bölen değil ÖLÇEK.
    - `kHistoryBonusScale = 64`: history sıralaması yalnız GÖRELİ değerlere baktığından
      çarpan sıralamayı değiştirmez. İDDİA DEĞİL, KANITLANDI: taze aramada d10 düğüm
      sayıları birebir aynı (startpos 87.436, Kiwipete 541.634). Tavana da çarpmaz
      (ölçekli maks ~10k << 700k). İki iş yapar: age()'in sinyali silmesini önler,
      history-LMR'nin bölenine granülarite verir.
    - Ölçekli dağılım: ort 224, maks 5-10k, |stat|>=1k olanlar %3-11.
      `kLmrStatDiv = 512` -> >=512 (~%7-11) 1 ply, >=1024 2 ply; sıfır çoğunluk etkilenmez.
    - Testler 102 -> 104. Sağlama (Release d10, kapı değil): startpos taze 87.436 ->
      62.541 (-%28); Kiwipete 541.634 -> 633.106 (+%17, ağaç yeniden şekillendi,
      bestmove d5e6 aynı). Aşırı-indirim erken uyarısı (>%60 düşüş) tetiklenmedi.
    - Mütevazı kazanç -> çok oyun istedi (razoring +16.5 / 2912 oyun ile aynı desen).
    - DERS: `4520bc1`'in eski "no-op" teşhisi ("malus ile örtüşme") kısmen YANLIŞTI;
      gerçek kök sebep tabloların her hamlede sıfırlanması + ölçek uyumsuzluğuydu.

**FAZ 2B EVALUATION TAMAM. Faz 2C selective search — PVS (Adım 1) + null move
(Adım 2, +56.3 Elo) + SEE (Adım 3, +27.8 Elo) + LMR (Adım 4, +164.5 Elo — açık ara
en büyük) + futility ailesi (Adım 5, +143 Elo — ikinci en büyük) + LMP (Adım 6,
+34.5 Elo) + razoring (Adım 7, +16.5 Elo) TAMAM, hepsi SPRT'den geçti. Check
extension (Adım 8) denendi, SPRT NÖTR -> rafa kaldırıldı. Move ordering: history
malus SPRT +22.2 Elo KABUL; **continuation history + tablo kalıcılığı paketi SPRT
+31.6 Elo TAM KABUL**; **history-tabanlı LMR + ölçek çarpanı SPRT +13.6 Elo TAM KABUL
(yeni baseline 9bdcef4) -> FAZ 2C-ek KAPANDI**; countermove denendi, geri alındı.
SIRADAKİ: pin-aware legal üretim (Faz 2C-hız) -> sonra Faz 2D (Lazy
SMP).** Tapered eval (+42.8), pawn structure
(+45.4), arama tekrar tespiti
(+27.2), piece mobility (H1), bishop pair + rook-on-file (H1) tam SPRT'den geçti;
king safety erken kabul (Elo +28.6 ± 18.6, kullanıcı kararı). Böylece Faz 2B'nin
gelişmiş evaluation kısmı bitti. Faz 2C sırası: **PVS** (LMR'nin çerçevesi, önce
bu) -> null move -> SEE -> LMR (en büyük Elo) -> futility ailesi -> LMP -> razoring
-> check extension (NÖTR, rafa) -> **history malus (KABUL +22.2)** -> continuation
history (sıradaki). Her biri ayrı commit. NOT: PVS
davranışı koruyan (exact) bir
optimizasyon olduğundan **SPRT KOŞULMAYACAK — kapı exactness ispatı** (sabit
derinlikte best move + score PVS öncesiyle birebir aynı + düğüm düşüşü); SPRT
LMR'ye saklandı (kullanıcı kararı). Faz 2D (Lazy SMP multi-threading) klasik fazın
son adımı, NNUE'dan önce. Yol haritası detayı için "Faz 2 — Klasik Güçlendirme"
bölümüne bak.

### Notlar

- Proje ilerledikçe "Şu an nerede olduğumuzu takip et" bölümünü elle güncelleyip
  commit'le, böylece her yeni oturum güncel durumu bilir.
- Perft referans değerleri için Chess Programming Wiki'deki "Perft Results"
  sayfası standart kaynak; ilgili pozisyonları ve derinlik değerlerini kontrol
  ederken oradan doğrula.
- **Copy-make bilinçli bir tercih, make/unmake'e geçilmeyecek.** `Board` 88 bayt
  (~1.5 cache line); kopyalaması make/unmake'in dallanmalı geri-alma defterinden
  ucuz. Ayrıca negamax'ın çok sayıda erken `return`/`continue` yolunda "unmake'i
  unutma" hata sınıfını tümden ortadan kaldırıyor (tekrar-anahtarı için yazmak
  zorunda kaldığımız `KeyGuard` RAII'si bu riskin kanıtı). Stockfish'in make/unmake
  kullanması bizim için argüman değil: onun `Position`'ı yüzlerce bayt (board[64],
  StateInfo'da checkers/pinners/blockers/checkSquares önbelleği) ve pin-aware
  legality sayesinde movegen'de zaten kopya yapmıyor. **NNUE bile make/unmake
  gerektirmez**: accumulator `Board`'a değil, Searcher'da ply-indeksli yığına konur
  (Stockfish'te accumulator StateInfo'da olduğu için make/unmake şart olmuş — yol
  bağımlılığı, doğa yasası değil). Hız yolu: **pin-aware legal üretim** (Faz 2C-hız).
