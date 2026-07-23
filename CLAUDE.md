# Satranç Motoru Projesi

Ben bir satranç motoru geliştiriyorum, hedefim Stockfish'in izlediği klasik yoldan
(alpha-beta search) başlayıp zamanla NNUE tabanlı evaluation'a geçmek. Performans
kritik olduğu için **C++** kullanacağız.

Bu bir tek seferlik kod üretimi değil, aylar sürecek iteratif bir proje. Sana rehberlik
edecek prensipler ve yol haritası aşağıda. Her oturumda bu bağlamı hatırla, ilerlemeyi
bu fazlara göre değerlendir.

**FORK (2026-07-10, `9d42bef`):** Bu repo artık **klasik taban**. NNUE işi ayrı bir
repoda yürüyor: `../ChessEngineNNUE` (github.com/muhtaraga/ChessEngineNNUE), bu reponun
git clone'u. İkisi de aktif. **NNUE bu repoda YOK — NNUE = NNUE reposunun işi (orada
Faz 3).** Klasik reponun Faz 1 + 2A-2D'si TAMAM; **Faz 2D sonrası yeni işi = Faz 3
(klasik) — Statik Değerlendirme (Eval) Güçlendirme** (aşağıya bak). SPRT'den geçen
search/movegen commit'leri NNUE reposuna `git fetch classical && git cherry-pick <sha>`
ile taşınır (Faz 2D borcu KAPANDI); eval terimleri de NNUE'ya cherry-pick ADAYI (NNUE
kendi eval'ini devreye alana kadar klasik eval terimleri orada da işe yarar — zorunlu
değil, kullanıcı kararına bağlı). `tools/sprt/*` yalnız BURADA değiştirilir (iki
kopyanın sürüklenmesi en sinsi risk).

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
- [x] (kozmetik) `seldepth` raporlama: quiescence + extension'larla ulaşılan en
      derin ply'ı takip edip `info depth x seldepth y` bas (Stockfish gibi).
      TAMAM (Blok 3/13, commit `caf663a`, EXACT). Ayrıntı: Blok 3 madde 13.

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

- [x] **Aşama 2 = Faz 2C-devam Blok 1/1 (DAVRANIŞ-KORUYAN; C1 `07a909a`, C2 `652d4b9`,
      C3 `d07e7f2`). SPRT +39.7 ± 15.6 Elo, LLR 2.95 TAM KABUL (1099 oyun, W-D-L
      380-464-255, LOS %100). YENİ BASELINE `d07e7f2`.** Ayrıntı: aşağıda Blok 1.

      **Yol haritası varsayımı ÇÜRÜTÜLDÜ.** Burada "üretim sırası değişir -> exact
      DEĞİL, kapı yalnız SPRT" yazıyordu; bu bir varsayımdı (Stockfish ayrı pinli/
      pinsiz döngü kullandığı için). Sırayı korumak maliyetsiz çıktı: her taş türü
      döngüsü aynen kalır, pinli taşın hedef maskesi `& line_bb(ksq, from)` ile
      daraltılır (taş başına bir `test_bit`). İki seçeneğin Elo beklentisi aynı
      (fark yalnız taş başına tahmin edilebilir bir dal); sıra korunduğunda
      **düğüm-sayısı eşitliği dedektörü** elde kalır — bu projede kanıtlanmış bir
      hata yakalayıcı (history-LMR'nin ilk no-op denemesini tam da bu yakaladı).
      Ders: "Stockfish öyle yapıyor" bir gerekçe değil; kısıtı kendi kodunda ölç.

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

**Faz 2C-devam — Tek-thread güçlendirme (YENİ, 2026-07-10; Faz 2D bundan sonra)**

Faz 2D'ye geçmeden önce tek thread'te kalan bütün değer toplanıyor. Kod keşfiyle
(search/eval/tt/movegen satır satır tarandı) üç kaynak envanterlendi: ertelenmiş
işler, motorda hiç olmayan standart teknikler, tuning fırsatları. Sıralama ilkesi:
(1) yapısal zorunluluk önce — TT format değişikliği 2D'nin thread-safe TT'sinden
ÖNCE bitmeli, Aşama 2 movegen değişikliği 2D + NNUE cherry-pick zincirinden önce
kapanmalı, eval-stack sonraki kapıların ön koşulu; (2) sonra Elo beklentisi.
Metodoloji aynı: her madde ayrı commit + ayrı SPRT; beklentiler mertebe tahmini,
söz değil (LMP/razoring dersi: mütevazı etki = çok oyun ister).

*Blok 1 — Yapısal + hız (sıra zorunlu):*

- [x] **1. Pin-aware Aşama 2 paketi (TAMAM, DAVRANIŞ-KORUYAN). SPRT +39.7 ± 15.6 Elo,
      LLR 2.95 TAM KABUL (1099 oyun, W-D-L 380-464-255, LOS %100). Base `127d4f0` vs
      new `d07e7f2`. YENİ BASELINE `d07e7f2`.**
      Üç commit, hepsi exact — kabul kapısı SPRT DEĞİLDİ, **düğüm sayısı / bestmove /
      skor / PV birebir eşitliği**. SPRT yalnız Elo'yu ölçtü.
      - `07a909a` **C1 — doğrudan pin-aware üretim**: `generate_legal`/`generate_noisy`
        tek geçişli oldu; `generate_pseudo`'nun 256-elemanlı geçici `MoveList`'i +
        hamle başına `is_legal` süzme geçişi kalktı. Hedef maskeleri (check_mask,
        pin ışını) doğrudan üretimde. Sıra korunur. `MoveGenContext` + `make_context`
        header'a açıldı. Çift çekte `check_mask = 0` -> şah dışı hiçbir taş üretilmez;
        **en passant hiçbir maskeye tabi değil** (tek sıradan iki taş kaldırır, pin
        makinesine görünmez -> tam occupancy testi, çift çekte bile denenir); pinli at
        asla oynayamaz; şah hamlesinde şah occupancy'den çıkarılır. `generate_castling`
        ikiye ayrıldı (`_unchecked` + sarmalayıcı). `generate_pseudo` +
        `generate_legal_reference` korundu (diferansiyel testin oracle'ı).
      - `652d4b9` **C2 — `CheckInfo` + kopyasız `gives_check()`**: check-squares
        (taş türü başına "buradan çek verilir" kareleri) + keşif-çeki `blockers`
        (sniper algoritmasının rakip şahtan çekilen aynası). Promosyon yeni taşla
        karar verir; en passant iki kare birden boşalttığından slider'ları yeni
        occupancy ile yeniden sorgular; **rok kasıtlı olarak copy-make referansına
        düşürülür** (şah+kale aynı anda iki kare boşaltıp iki kare doldurur, bit
        hilesi tuzaklı; düğüm başına en fazla iki rok hamlesi var — doğruluk >
        mikro-hız).
      - `d07e7f2` **C3 — search wiring**: negamax düğüm girişinde `make_context` bir
        kez; `in_check = ctx.checkers != 0` bedavaya gelir -> düğüm başına İKİ
        `is_square_attacked` çağrısı kalktı (`terminal_score` kaldırıldı, mat/pat
        doğrudan `in_check`'ten). Hamle döngüsünde `Board next` artık budama
        kapılarından SONRA kuruluyor — futility/LMP ile atılan hamle kopya + do_move
        + is_square_attacked bedelini artık ödemiyor. `CheckInfo` **tembel** kurulur:
        `gives_check` yalnız `i >= 1`'de sorulur (futility `moves_searched>0` ister,
        LMP eşiği >= 4, LMR `i >= 2`), yani ilk hamlede kesme yapan düğümlerde —
        iyi sıralamada çoğunluk — hiç ödenmez. **quiescence bilinçli olarak
        değiştirilmedi**: stand-pat beta kesmesi yolunda `make_context`, bugünkü ucuz
        `is_square_attacked`'den pahalı olurdu ve orada üretim hiç yapılmıyor.

      Kapılar (üçü de geçti): (1) perft birebir — startpos d6 = 119.060.324,
      Kiwipete d5 = 193.690.690; (2) diferansiyel test — perft ağacında
      `generate_legal == generate_legal_reference` AYNI SIRADA, `generate_noisy ==`
      süzülmüş `generate_legal` AYNI SIRADA, **ve her legal hamle için
      `gives_check(b,m,ci) == { next=b.do_move(m); is_square_attacked(...) }`**
      (aramanın eski hesabının tam ifadesi -> C3'ün exact olduğunun ispatı); yeni
      kenar FEN'leri: rok çek verir (kısa/uzun), promosyon (b8=Q/R çek verir, b8=B/N
      vermez; c8=N verir, c8=Q vermez), ep keşif çeki (exd6 e.p. aynı anda d5+e5'i
      boşaltır, Rh5 Ka5'i görür — keşif testi tek başına yakalayamaz); ayrıca
      `MoveGen.GivesCheckSpecialCases` (diferansiyel test "gives_check == referans"
      der; FEN'ler gerçekten çek ÜRETMEZSE boş yere geçerdi). 105 -> 106 test.
      (3) **düğüm eşitliği** — Release, `127d4f0`'a karşı, `ucinewgame` ile taze
      tablolar: startpos d10 = 62.541, d13 = 627.044; Kiwipete d12 = 1.590.343;
      bestmove + skor + PV birebir.

      Ölçüm (Release, aynı oturumda `127d4f0`'a karşı):
      - perft startpos d6: 0.915 s -> 0.536 s (130.1M -> 222.1M nps, **1.73×**)
      - perft Kiwipete d5: 1.133 s -> 0.630 s (170.9M -> 307.6M nps, **1.79×**)
      - **arama** startpos d13 nps: 1.894M -> 2.224M (**+%17.4**)
      - **arama** Kiwipete d12 nps: 2.384M -> 2.714M (**+%13.8**)

      Not: movegen 1.75× hızlandı ama arama yalnız ~%15 — çünkü aramada movegen
      artık toplam sürenin küçük bir dilimi (eval + do_move + TT baskın).

      **DÖVİZ KURU İKİNCİ KEZ DOĞRULANDI.** `log2(1+X) × 195` ile önceden **+35..45
      Elo** tahmin edilmişti (%17.4 / %13.8 nps); gerçekleşen **+39.7 ± 15.6** —
      bandın tam ortası. Aşama 1'de de tahmin (+100-200) tutmuştu (+126.8). Kur artık
      kalibre bir araç: bir nps optimizasyonunun Elo'sunu KODU YAZMADAN, yalnız
      benchmark'tan kestirebiliyoruz. Mütevazı etki -> çok oyun istedi (1099; LMP
      +34.5 / 1413 oyun deseni).
- [x] **2. TT yenileme paketi (TAMAM). İki SPRT, toplam ~+46 Elo. Beş commit
      (`1d73725` B1 .. `23d28b0` B5). YENİ BASELINE `23d28b0`.** Yol haritasının
      +10-25 beklentisini aştı. İki bağımsız kabul kapısı:
      - **SPRT-A: base `d07e7f2` vs new `bf462a3` (B1..B4), +12.9 ± 8.2 Elo, LLR 2.95
        TAM KABUL (4024 oyun, 1254-1665-1105, LOS %99.9).** LMP/razoring gibi çok
        oyun istedi. B1+B2 exact (nps), B3+B4 sezgisel.
      - **SPRT-B: base `bf462a3` vs new `23d28b0` (yalnız B5 qsearch TT), +33 ± 14.1
        Elo, LLR 2.96 TAM KABUL (1438 oyun, 510-554-374, LOS %100).** "İşareti belirsiz,
        birçok motorda net nötr" beklentisi ÇÜRÜTÜLDÜ — paketin en büyük parçası.
      Commit'ler:
      - `1d73725` **B1 (exact)**: TTEntry 24 -> 16 bayt (score i32->i16, depth i16->i8,
        bound+gen tek bayta: üst 6 bit nesil / alt 2 bit Bound) + i16 `eval` alanı (ham
        statik eval; çektekilerde `kEvalNone`=INT16_MIN). Anahtar dışı veri TAM 8 bayt
        -> **Faz 2D lockless-XOR geometrisi oturdu**. Aynı Hash MB'sinde 2× giriş, cache
        line'a 4 giriş. TUZAK: nesil maskesi SAYAÇTA (`new_search`'te `&0x3F`) —
        girişte 6 bit var, sayaç 63'ü geçerse hiçbir giriş "taze" görünmez, TT sessizce
        çöker. store'da depth 127'ye kırpılır (`go depth 200` int8'i sarardı).
      - `57cc883` **B2 (exact)**: `static_eval` TT'den okunur (evaluate() saf -> birebir;
        nps +%2.5 / +%2.1).
      - `8338adc` **B3 (sezgisel)**: TT skoru razoring/RFP/futility eval'ini rafine eder
        (sınırın izin verdiği yönde; mat skorları hariç). **Rafine değer TT'ye ASLA
        yazılmaz** (store ham static_eval'i yazar) — yazılsaydı sonraki sonda rafineyi
        tekrar rafine eder, hata birikirdi. Blok 1/3 improving de ham değeri isteyecek.
      - `bf462a3` **B4 (sezgisel)**: replace kuralından "aynı pozisyon DAİMA ezer"
        kaldırıldı; sığ/exact-olmayan giriş derin girişi ezmiyor (B5'in ön koşulu — qs
        girişleri depth 0). `(e.key==key && bound==EXACT)` muaf.
      - `23d28b0` **B5 (sezgisel)**: qsearch TT probe/store (depth 0; EXACT saklanmaz —
        PV-düğüm bayrağı yok; mat saklanmaz) + tt_move sıralaması. **Güvenlik ağı:**
        negamax `tte.depth >= depth (>=1)` ister -> qs girişi (depth 0) negamax'ı asla
        kesemez, yalnız hamle ipucu + ham eval verir.

      **Düğüm eşitliği kapısı (B1+B2, geçti):** entry 24->16 gidince aynı Hash MB'sinde
      giriş sayısı 2× olur -> Hash=16 vs Hash=16 exact DEĞİL. Giriş sayısı eşitlenir:
      baseline Hash=16 (16MB/24 -> 524288) vs yeni **Hash=8** (8MB/16 -> 524288).
      startpos d13 = 627.044, Kiwipete d12 = 1.590.343 (d16/d14 de) birebir; bestmove+
      skor+PV aynı. Ders: format değişince düğüm dedektörünü korumak için giriş sayısını
      eşitle, MB'yi değil.

      **B5 profili (ilginç):** nps DÜŞÜYOR (-%6.7 startpos / -%21.7 Kiwipete; her qs
      düğümüne rastgele bellek erişimi) ama düğüm -%38 / -%47 -> aynı derinliğe süre
      -%34 / -%32. Döviz kuru burada UYGULANAMAZ (yalnız EXACT nps kazançları için
      kalibre; B5 düğüm başına yapılan işi de değiştirir — arama yerine önbellek okur).
      +33 Elo bunu doğruladı: düğüm kalitesi kazancı nps kaybını fazlasıyla götürdü.

      Testler 106 -> 115. Bilinçli ertelenen: TT prefetch (`_mm_prefetch`, exact-safe,
      ~%1-3), PV-node bayrağı (qs'te EXACT saklamayı açar), 4-yollu bucket (Faz 2D),
      RFP/null erken dönüşlerinde "yalnız eval" girişi saklama, tt_move'un qs SEE
      budamasını atlaması.
- [~] **3. Eval stack + improving — DENENDİ, RAFA KALDIRILDI (SPRT ~NÖTR).**
      Ayrı `stack_eval[MAX_PLY]` dizisi (çekteyken kEvalNone sentinel); `improving =
      static_eval > 2 ply önceki (ayni taraf), 2 ply çekteyse 4 ply'e düşen zincir`.
      Üç budama kapısı improving'e bağlandı (Stockfish deseni): RFP marjı
      `kRfpMargin*(depth-improving)`, futility improving'de +60 bonus, LMP
      non-improving'de eşik düşer.
      - **İlk sürüm (`c73400f`): SPRT H0 (5+0.05, elo0=0/elo1=5, LLR −2.94 geçti).**
        Net-negatif/nötr-altı.
      - **Teşhis: kod hatası değil, kalibrasyon + PAKETLEME hatası.** Üç kaplama tek
        SPRT'de → hangisi zararlı ayrışamıyor. Geçici enstrümantasyonla (5 pozisyon,
        depth 12) ölçüldü: LMP non-improving eşiğini `base/2` yapmak tüm LMP
        budamalarının ~%71'ini üretiyordu (SPRT'den geçmiş `3+d²`'yi düğümlerin
        ~yarısında ikiye bölmek = dominant regresyon); futility +60 yalnız %7 etki
        (saf düğüm maliyeti); RFP `depth-improving` %35.5 extra ama mutlak olarak
        LMP'nin ~1/12'si.
      - **Revize (`2ce0352`): futility bonusu kaldırıldı, LMP `base/2`→`(3·base)/4`,
        RFP+altyapı korundu. SPRT: 10.000 oyun tavanına takıldı (5+0.05), Elo
        +1.2 ± 5.3, LLR −0.876, LOS %67 — kalibrasyon zararı giderdi ama kazanç
        açmadı (~NÖTR).**
      - **Ders 1 (check extension emsali):** çok-budayan yığında (LMR+null+futility+
        LMP+razoring) budama kapıları improving-farkındalığı olmadan zaten "yeterince
        iyi"; improving sömürülebilir seçicilik eklemiyor.
      - **Ders 2 (metodoloji):** tek SPRT'de birden çok bağımsız sezgisel kaplama
        PAKETLENMEZ — H0'da sinyal ayrışmaz. "improving tek yol haritası maddesi"
        gerekçesi yanıltıcıydı (madde tek, kaplamalar üç ayrı budama ayarı). Cont-hist
        paketi emsal DEĞİL (orası tek mekanizmanın önkoşul zinciriydi).
      - **Ders 3 (SPRT gücü):** 5+0.05 + elo0=0/elo1=5 ile <5 Elo etki temiz karara
        varamaz (~0 daima 10k'da tavana takılır). Sub-5 aday teknikler için ya daha
        sıkı sınır ya sabit-N gerekir.
      - **İki commit (`c73400f`, `2ce0352`) `23d28b0`'a birebir geri alındı** (kaplamalar
        + altyapı söküldü; `git diff 23d28b0 -- src/search.cpp tests/search_test.cpp`
        boş → davranış-eş). Testler 115'e döndü. İLERİDE ADAY: Blok 4 Texel tuning
        sonrası eval kalibre olunca improving sinyali daha anlamlı olabilir; yalnız
        RFP kaplaması (en masum) tek başına da yeniden denenebilir.

*Blok 2 — Arama özellikleri (Elo beklenti sırası):*

- [x] **4. Singular extension — TAMAM, SPRT GEÇTİ H1 (+21.3 ± 11.1 Elo, LLR 2.94
      TAM KABUL, 2274 oyun, 753-907-614, LOS %100). Base `23d28b0` vs new `a803a3f`.
      YENİ BASELINE `a803a3f`.** Beklenen +20-40 bandının alt ucunda; rafa kaldırılan
      check extension'ın "doğru hali" doğrulandı — kör "tüm çekleri uzat" NÖTR'dü,
      "yalnız kanıtlanmış-tekil tt_move'u uzat" +21.3 verdi.
      - **Mekanik:** tt_move'u aramadan önce, azaltılmış derinlikte (`(depth-1)/2`)
        tt_move'u DIŞLAYAN null-window [sb-1, sb] doğrulama (`sb = ttValue -
        kSingularMargin*depth`); hiçbir alternatif sb'ye ulaşamıyorsa (fail-low)
        tt_move tekildir -> `extension=1`, `new_depth = depth-1+1` dört child
        çağrısına (PVS ilk, LMR scout/re-search, PVS re-search).
      - **`excluded` parametresi** (negamax son param, default Move()): doğrulama
        aynı `b`'yi tt_move'suz yeniden arar. excluded iken prolog kısayolları
        ATLANIR — repetition + 50-hamle + TT-cutoff + KeyGuard-push + TT-store.
        KRİTİK: dış düğüm `b.key`'i yığına zaten push etti; atlanmasaydı (a)
        `is_repetition(b)` false-positive "tekrar->0" döner, (b) aynı-key TT girdisi
        reduced derinlikte `tte.depth>=depth` sağlayıp anında ttValue döndürür ->
        singular ASLA ateşlemez. `std::optional<KeyGuard>` ile çift-push engellendi.
        PV/`stack[ply]` kirlenmesi tt_move'un hemen ardından gelen gerçek aramasıyla
        yeniden yazıldığından zararsız (analiz edildi).
      - **Gate:** excluded yok && m == tt_move (i==0'ı sağlamlaştırır, TT collision'da
        tt_move illegal olabilir) && ply>0 && depth>=kSingularMinDepth(8) &&
        tte.depth>=depth-kSingularDepthMargin(3) && (LOWER || EXACT) && mat-olmayan
        ttValue. Doğrulama null-move kapalı (`null_allowed=false`).
      - Sabitler (min-depth 8, depth-margin 3, margin 2) ilk elle-seçim, Blok 4/16
        tuning. 2 test (115->117): SingularKeepsWinningTactic/MateSearch. Sağlama
        (Release, taze): startpos d13 +%79 (e2e4), Kiwipete d12 +%76 (e2a6, cp-55->
        -12 keskinleşti) — düğüm artışı beklenen (uzatma + doğrulama maliyeti),
        >2× patlama yok. Ertelenen: **multicut** (`sb >= beta` iken doğrulama
        fail-high -> dalı buda; ayrı commit + SPRT), negatif/çift/üçlü extension.
- [x] **5. SEE paketi (main search) — TAMAM, İKİ COMMIT/İKİ SPRT, TOPLAM ~+56 Elo.
      Base `a803a3f`; YENİ BASELINE `8fa2281`.** Beklenti +15-30 idi, net aşıldı. İki
      parça bilinçle AYRI SPRT'den geçti (Blok 1/3 dersi: bağımsız sezgisel kaplamalar
      paketlenmez; önkoşul zinciri değiller — budama sıralamayı gerektirmez).
      - `dd9e8f3` **Commit 1 — kayıplı yakalama sıralaması**: `score_move` promosyon-olmayan
        yakalamaları SEE işaretine ayırır; `see<0` (savunmalı taşa vurma) quiet history
        bandının (-700k) ALTINA `kScoreBadCapture`=-1M bandına iner, `see>=0` yüksek
        bantta kalır (her ikisi MVV-LVA sıralı). Promosyonlar SEE'den muaf. Kayıplı
        yakalamalar yalnız yeniden SIRALANIR, asla budanmaz -> taktik kaybolmaz. Perft
        birebir; nps ~±3% (see() ordering maliyeti ihmal edilebilir). **SPRT: +33.1 ± 14.1
        Elo, LLR 2.96 TAM KABUL (1420 oyun, 501-553-366, LOS %100)** — "mütevazı, çok oyun
        ister" beklentisini net aştı; tek başına paketin tahminini karşıladı.
      - `8fa2281` **Commit 2 — sığ-derinlik SEE budaması**: negamax döngüsünde futility/LMP
        yanına, `depth<=8` düğümde SEE'si çok kötü hamleleri eler — yakalama `see <
        -20*depth*depth`, quiet `see < -65*depth` (see() sessiz hamleyi de değerlendirir,
        check-extension'dan kalan latent altyapı). Güvenlik: `moves_searched>0` (ilk/PVS
        hamle daima), `!gives_ck`, mat penceresi kapalı, `ply>0`, promosyon hariç (see()
        promosyonu desteklemez). Perft birebir; node sanity startpos d13 921K->612K (-34%),
        Kiwipete d12 1131K->984K (-13%), bestmove makul/skor sabit. **SPRT: +23.3 ± 11.7
        Elo, LLR 2.94 TAM KABUL (1972 oyun, 636-832-504, LOS %100).**
      Testler 117 -> 120: SeeOrderingKeepsWinningSacrifice (bağlı savunucuya rağmen Qxe5,
      SEE bağı görmez see=-580 -> kötü-yakalama bandı, yine de bulunur — demote budama
      değil), SeePruningKeepsWinningTactic (bedava vezir Rxe5 see>0 budanmaz),
      SeePruningKeepsMateSearch (mat penceresi kapalı). Sabitler (kSeeMaxDepth 8,
      kSeeCaptureMargin 20, kSeeQuietMargin 65, kScoreBadCapture -1M) ilk elle-seçim,
      Blok 4/16 tuning. Ertelenen: see_ge (yalnız işaret, tam swap'tan ucuz), düğüm-başı
      see cache'i (Commit 1 ordering + Commit 2 döngü late hamleleri iki kez see çağırır).
- [~] **6. Capture history — DENENDİ, RAFA KALDIRILDI (SPRT H0, -16.2 ± 11 Elo,
      LLR -2.95 TAM RED, 2236 oyun, 605-922-709, LOS %0.2). Base `8fa2281`'e birebir
      geri alındı (commit'ler `8bb5564`+2 test-düzeltme -> `src/search.cpp` yeniden
      `8fa2281`).** `capt_hist[taş(0..11)][hedef][alınan tür]` (inline ~18KB); score_move
      non-promo yakalama dalında materyal-ölçekli MVV + `ch/kCaptHistOrderDiv`;
      `update_capture_stats` (quiet deseni yansıması, killer/cont yok); SEE-işaret bandı
      korunuyordu (bant-içi sıralama). En passant PAWN özel-durumu (OOB) ele alındı.
      **KRİTİK DERS (düğüm ≠ Elo): düğüm sağlaması NET İYİYDİ** — materyal-ölçekli MVV +
      bölen 256 ile çeşitli açılış pozisyonlarında agregat düğüm **0.83×** (Kiwipete
      **0.64×**, yani DAHA AZ düğüm), yine de -16 Elo. İlk kalibrasyon (ölçeksiz mvv +
      bölen 8) düğümü 2.15× şişirmişti; onu düzeltip node-pozitif hale getirdim ama Elo
      yine negatif. Sezgisel (davranış-değiştiren) yeniden-sıralamada **düğüm sayısı Elo
      proxy'si DEĞİL**: kur/döviz mantığı yalnız EXACT hızlanmalara uygulanır; capture
      history HANGİ düğümlerin arandığını değiştirir, az düğüm = daha kötü hamle kalitesi
      olabilir (budama/sıralama iyi hatları eledi). MVV-LVA+SEE zaten çok güçlü; öğrenilmiş
      capture sinyali marjinal ve bu tabanda net-negatif çıktı. İLERİDE ADAY: farklı
      tasarım (capture history'yi SEE-budama marjına katmak, ya da Texel tuning [Blok 4]
      sonrası eval kalibre olunca), veya Stockfish'in tam `update_all_stats` varyantı
      (quiet-cutoff'ta da capture cezası). Testler `8fa2281`'e döndü (123 -> 120).
- [x] **7. IIR (Internal Iterative Reduction) — TAMAM, SPRT GEÇTİ H1 (+15 ± 9 Elo,
      LLR 2.96 TAM KABUL, 3299 oyun, 1021-1399-879, LOS %99.9). Base `8fa2281` vs new
      `3bde658`. YENİ BASELINE `3bde658`.** Beklenti (+5-15) bandının tam üst ucu.
      - **Mekanik:** TT hamlesi olmayan (miss ya da move'suz hit), kök-dışı (ply>0),
        yeterince derin (`depth >= kIirMinDepth=4`) düğümde `depth -= 1`. Null-move
        bloğundan SONRA, move döngüsünden ÖNCE -> RFP/razoring/null orijinal derinlikle
        çalışır (tuned gate'ler korunur); azaltılmış derinlik yalnız çocuk-arama +
        döngü-içi LMR/LMP/futility kapılarına yansır. `ply>0`: kök daima tam aranır
        (dosyadaki tüm kapılarla tutarlı; gerçek oyunda kök zaten ID'den tt_move taşır).
        tt_move VARKEN singular extension devreye girer -> IIR ile karşılıklı dışlayıcı.
        Sabit `kIirMinDepth=4` ilk elle-seçim, Blok 4/16 tuning.
      - **TT-durumu bağımlılığı (yeni sınıf):** IIR, arama davranışını TT-durumuna
        bağımlı kılan İLK sezgisel (indirim tt_move VARLIĞINA bakar). İki TT testinin
        "TT'den bağımsız determinizm" ön kabulünü geçersiz kıldı (IIR'li her motorda
        böyle). `ResultConsistentAcrossTTState` taze determinizm (a==c, skor dahil) +
        sıcak-TT bestmove kararlılığına (a.best==b.best) daraltıldı, soğuk-vs-sıcak SKOR
        eşitliği kaldırıldı; `ReducesNodesOnResearch` depth 3'e (< kIirMinDepth, IIR
        kapalı) indirildi (IIR aktifken soğuk TT agresif indirim aldığından "sıcak TT az
        düğüm" premisi tersine döner). 2 yeni test (IirKeepsWinningTactic/MateSearch),
        120 -> 122.
      - **Düğüm sağlaması KARIŞIK ÇIKTI (kur/döviz İ TERS: heuristik):** Release,
        `8fa2281`'e karşı — startpos d13 612K->905K (**+%48**, bestmove b1c3->e2e4),
        Kiwipete d12 984K->851K (-%13, e2a6 aynı). "Düğüm düşer" beklentisi TUTMADI ama
        Elo yine +15 -> Blok 2/6 dersi tekrar doğrulandı: sezgisel yeniden-şekillendirmede
        düğüm ≠ Elo, döviz kuru yalnız EXACT hızlanmalara. Ertelenen: daha yüksek
        kIirMinDepth (6-8) / yüksek derinlikte reduce-by-2 (startpos +%48 fazla-sığ
        ateşlemeyi ima ediyor, tuning adayı).
- [x] **8. History-tabanlı quiet budaması — TAMAM, SPRT GEÇTİ H1 (+14.3 ± 8.7 Elo,
      LLR 2.96 TAM KABUL, 3575 oyun, 1129-1464-982, LOS %99.9). Base `3bde658` vs
      new `a8ac0d9`. YENİ BASELINE `a8ac0d9`.** Beklenti (+10-20) tam ortası.
      - **Mekanik:** sığ, çekte-olmayan, mat-olmayan düğümde (`can_hist_prune`:
        `!in_check && ply>0 && depth<=kHistPruneMaxDepth(4) && !is_mate_score(α/β)`),
        döngüde `moves_searched>0 && quiet && !gives_ck` iken ham `stat = main +
        cont` (history-LMR ile AYNI hesap) `< -kHistPruneMargin(2000)*depth` ise
        `continue`. SEE budamasından ÖNCE yerleştirildi (history iki dizi okuması
        ucuz, SEE swap algoritması pahalı -> kötü-history hamle SEE'ye hiç girmesin).
        LMP'nin (sayı-tabanlı) + SEE-quiet budamasının (materyal-tabanlı) içerik-
        tabanlı tamamlayıcısı: "bu hamle bu bağlamda geçmişte hep başarısız oldu".
      - **Güvenlik (mevcut kapı deseni):** moves_searched>0 (i==0/PV daima aranır),
        quiet (yakalama/promosyon dokunulmaz), !gives_ck (çek/taktik korunur),
        !is_mate_score (mat penceresinde kapalı; INF>MATE_IN_MAX -> kök geniş pencere
        doğal dışlanır).
      - **Sabitler ÖLÇÜLEREK seçildi** (geçici enstrümantasyon, commit'te yok;
        kLmrStatDiv emsali): eligible late-quiet dağılımında (depth≤4) mevcut gate
        **%2.06 buduyor** (no-op DEĞİL), derinlik 1'de en kötü ~%18'i, derinlik 4'e
        doğru ~sıfıra sönüyor -> roadmap'in "**çok kötü** quiet'leri **sığ
        derinlikte**" tarifine tam oturuyor. Raw stat kuyruğu: stat<-1000 %23,
        <-2000 %18, <-4000 %5, <-8000 %0.02. margin 2000 / maxdepth 4 ilk elle-seçim
        ama artık ölçümle gerekçeli; Blok 4/16 SPSA adayı.
      - 2 test (122->124): HistPruningKeepsWinningTactic (bedava vezir hâlâ Rxe5 —
        yakalama quiet değil, budanmaz), HistPruningKeepsMateSearch (arka sıra matı
        korunur). Node sanity (Release, `3bde658` base, taze tablolar): startpos d13
        905K->796K (-%12, e2e4 aynı), Kiwipete d12 851K->823K (-%3.3, e2a6 aynı),
        skor sabit, patlama yok. Düğüm != Elo (Blok 2/6 dersi) -> kabul kapısı SPRT.
        Ertelenen: derinlik-kare margin (şu an lineer), ayrı/çoklu cont-hist katmanı,
        improving-farkındalığı (Blok 1/3 rafta).
- [x] **9. Null move güçlendirme — KISMEN TAMAM (Commit 1 KABUL, Commit 2 RAFA).
      İki commit/iki SPRT. YENİ BASELINE `a2a6bfa`.** Beklenti (+5-15) tek başına
      Commit 1 ile aşıldı.
      - `a2a6bfa` **Commit 1 — `eval>=beta` kapısı + dinamik R (KABUL). SPRT +21.9 ±
        11.3 Elo, LLR 2.95 TAM KABUL (2032 oyun, 629-902-501, LOS %100).** Önkoşul
        zinciri olarak tek commit (cont-hist emsali; kapı `eval>=beta` dinamik R'nin
        `(eval-beta)` terimini >= 0 garanti eder — improving-paketleme hatası değil).
        Gate'e `eval >= beta` (statik eval beta altındaysa null boşuna denenir); sabit
        `(depth>=6)?3:2` yerine `R = kNullBaseR + depth/kNullDepthDiv +
        min((eval-beta)/kNullEvalDiv, kNullEvalMaxR)`; çocuk derinliği `depth-R` (eski
        `depth-1-R`; R artık tam indirim), `max(...,1)`. Sabitler (kNullMinDepth 3,
        kNullBaseR 3, kNullDepthDiv 3, kNullEvalDiv 200, kNullEvalMaxR 3) ilk elle-seçim,
        Blok 4/16 tuning. 1 test (124->125: NullMoveGateKeepsWinningTactic). Düğüm
        sağlaması (Release, `a8ac0d9`'a karşı): startpos d13 -%16, Kiwipete d12 -%13,
        bestmove aynı (daha agresif null).
      - `3f0f5ac` **Commit 2 — verification search — DENENDİ, RAFA KALDIRILDI (SPRT
        NÖTR, +1.8 ± 9.6, LLR -0.136, 2654 oyun, 709-1250-695, LOS %64.6). `src/search.cpp`
        + test `a2a6bfa`'ya birebir geri alındı (`f3f48e6`; `git diff a2a6bfa` boş).**
        `depth >= kNullVerifyMinDepth(12)` null fail-high'ında null'suz azaltılmış aramayla
        teyit; teyit de >= beta ise buda, aksi halde zugzwang şüphesi -> normal ara. Aynı
        `b`'yi aynı ply'de yeniden aradığından singular'ın `excluded` prolog-atlama yolu
        `Move::make(B1,B1)` sentinel'iyle (from==to -> hiç üretilmez -> gerçek hamle
        dışlamaz; nonzero -> Move()'dan farklı) yeniden kullanıldı — ek altyapı yok.
        **DERS (check extension emsali + Blok 1/3 Ders 3):** kNullVerifyMinDepth=12
        verification'ı yalnız derin midgame'de ateşlettiğinden hızlı TC'de nadir tetikler
        (düğüm sağlaması: d13/d12 ~aynı, d16 aktif -%6.2); çok-budayan yığında zugzwang
        güvenlik ağı marjinal. elo0=0/elo1=5 ile ~0 etki temiz karara varamaz (LLR sıfır
        civarı sürünür, 10k tavanı) -> kullanıcı kararıyla durduruldu. İLERİDE ADAY:
        düşük kNullVerifyMinDepth (8-10) ya da Texel sonrası; ayrıca fail-soft null
        (`return score`) hâlâ denenmedi.
- [ ] **10. ProbCut** (opsiyonel): beta+margin etrafında sığ arama/qsearch
      fail-high verirse buda. 4-9 sonrası hâlâ değer veriyorsa denenir. +5-15.

*Blok 3 — Zaman yönetimi + küçükler:*

- [x] **11. Adaptif zaman yönetimi (best-move stability) — TAMAM, SPRT GEÇTİ H1
      (+40.6 ± 15.8 Elo, LLR 2.96 TAM KABUL, 1022 oyun, 341-459-222, LOS %100).
      Base `a2a6bfa` vs new `a44d6eb`. YENİ BASELINE `a44d6eb`.** Beklenti (+5-20)
      NET AŞILDI — roadmap'in "hızlı TC'de belirgin" öngörüsü doğrulandı; hızlı
      SPRT TC'sinde (5+0.05) zaman yönetimi büyük kaldıraç.
      - **Mekanik:** kök best-move'un derinlikler boyunca kararlılığı
        (`stability` = art arda değişmeden kalınan tamamlanan derinlik sayısı) soft
        limiti ölçekler. `time_scale(stability)` monoton azalan, `[0.5, 1.5]`
        sınırlı saf fonksiyon (`kTimeStabMax 1.5 / Step 0.13 / Cap 8 / Floor 0.5`);
        kararsız (0) -> 1.5 (uzat), doygun (8) -> 0.5 (kıs). `search_iterative`
        soft-break: `eff = soft * time_scale(stability)`, `hard_ms` tavanlı. Tek
        mekanizma iki yöne de hizmet eder (kısma + uzatma).
      - **Uygunluk kapısı (kritik doğruluk):** `SearchLimits::adaptive_time` yalnız
        timed-game (wtime/btime) dalında (`handle_go`) true. movetime/depth/
        infinite/default budget'ta `soft==hard` (ya da -1) olduğundan ölçekleme
        yanlış olur -> o modlar BİREBİR dokunulmaz. Ek güvenlik: `hard_ms >
        soft_ms` gerekir (geç oyunda `max_use` clamp'i `hard≈soft` yaparsa statiğe
        düşer). Time forfeit riski yok: ölçek ≤1.5, `hard_ms` mutlak tavan korunur.
      - **Kapsam:** yalnız best-move stability. Fail-low / düşen-eval uzatması
        AYRI sinyal -> ertelendi (Blok 1/3 dersi: bağımsız sezgiselleri paketleme).
      - Sabitler ilk elle-seçim, Blok 4/16 SPSA. 2 test (125->127):
        `TimeScaleMonotonicAndBounded` (yön hatası = time-trouble, saf fonksiyonla
        yakalanır), `AdaptiveTimeInertWithoutTimeLimit` (bayrak set + zaman sınırsız
        -> arama BİREBİR aynı, TT temizlenerek düğüm dahil). Manuel UCI (soft~2000/
        hard~6000): startpos ~6000ms (uzat), R-vs-R ~2388ms, KPvK sabit ~1212ms
        (soft ALTI, kıs), `movetime 2000` ~2000ms (dokunulmaz). Ertelenen: fail-low
        uzatması, düşen-eval faktörü, non-linear instability (Stockfish bestMoveChanges).
- [~] **12. Tempo bonusu — DENENDİ, RAFA KALDIRILDI (SPRT H0). `evaluate()`'e
      `TempoBonus=15` (stm-relative, flip'ten sonra) eklendi (`71127ce`); SPRT
      -18.6 ± 13 Elo, LLR -2.41 (H0 yönünde, LOS %0.2, 1399 oyun) -> kullanıcı
      kararıyla durduruldu, `a44d6eb`'ye birebir geri alındı (revert `149cc92`;
      `git diff a44d6eb -- src include tests` boş). Testler 128->127.**
      **DERS (improving/capture-history emsali): tempo `evaluate()` içinde olunca
      yalnız yaprak değeri değil, budama kapılarının besleme kaynağı `static_eval`'i
      de +15 kaydırır.** RFP (`static_eval - 80·depth >= beta` -> `return static_eval`)
      sığ her sessiz düğümde ateşler VE şişirilmiş skoru döndürür -> over-prune +
      şişik fail-high; null gate (`eval>=beta`) da agresifleşir. Bu marjlar
      (RFP 80·d, futility, razor, null) hepsi elle seçilmiş, tempo'suz kalibre ->
      uniform +15 dengeyi bozdu. Kod hatası DEĞİL (testler simetrik pozisyonda tam
      +15 doğruladı); saf kalibrasyon. İLERİDE ADAY: (a) Blok 4 Texel sonrası marjlar
      tempo ile birlikte kalibre olunca, (b) cerrahi varyant — tempo yalnız yaprak/
      qsearch stand-pat dönüşüne, budama kapıları tempo'suz `static_eval` üzerinde.
- [~] **13. Fırsat işleri** (büyük işler arasında tek tek). İki kalem TAMAM
      (hijyen paketi, ikisi de SPRT'siz — biri kozmetik, biri near-exact):
      - [x] **seldepth raporlama (kozmetik, EXACT) — commit `caf663a`.** Ulaşılan
        en derin ply (qsearch + extension) takip edilip `info depth X seldepth Y`
        basılır. Arama davranışına SIFIR etki: `Searcher.seldepth` yalnız negamax/
        quiescence girişinde max ile güncellenir, hiçbir karar okumaz. `SearchResult`
        alanı; `search_iterative` her iterasyon başında sıfırlar. Test:
        SeldepthAtLeastDepth (127->128). Kapı EXACT: `a44d6eb`'a karşı startpos d13
        (668137 düğüm) + Kiwipete d12 (715497) düğüm/bestmove/skor/PV birebir aynı.
      - [x] **mate distance pruning (near-exact) — commit `9307674`.** negamax'ta
        yaprak kontrolünden sonra, TT sondasından ÖNCE, `ply>0` guard:
        `alpha=max(alpha,-MATE+ply); beta=min(beta,MATE-(ply+1)); if(alpha>=beta)
        return alpha;`. Yalnız pencere mat sınırlarına yakınken (mat alt-ağaçları)
        ısırır -> non-mate aramada bounds değişmez. excluded (singular) iken
        tt_value/singular_beta mat-olmayan -> no-op. Test: MateDistancePruningKeepsMate
        (128->129). Kapı near-exact (SPRT YOK; ~+2 Elo 5+0.05'te temiz karar veremez,
        Blok 1/3 Ders 3): non-mate birebir (yukarıdaki iki pozisyon) + MAT node-drop
        (iki-kale matı d4 1357->662 düğüm ~%51, skor/ilk-hamle aynı) + tüm mat testleri.
      - [x] **delta pruning (qsearch) — TAMAM, SPRT GEÇTİ H1 (+7.8 ± 5.8 Elo, LLR
        2.96 TAM KABUL, 7192 oyun, 1989-3375-1828, LOS %99.5). Base `a44d6eb` vs new
        `94c5b95`. YENİ BASELINE `94c5b95`.** Beklenti (+5-15) tam ortası; mütevazı
        etki -> çok oyun istedi (razoring +16.5/2912 oyun deseni, ~2× daha da fazla).
        - **Mekanik:** qsearch'in çekte-olmayan dalında, ana döngüde (selection
          sort'tan SONRA, do_move'dan ÖNCE), promosyon-olmayan yakalamalar için üst
          sınır `raw_eval + MaterialValue[victim] + kDeltaMargin(200) <= alpha` ise
          `continue`. `raw_eval` bu dalda stand_pat değerini tutar (çekteyken
          kEvalNone -> gate zaten `!in_check` ile kapalı). `victim` = ep'te PAWN,
          aksi `b.type_on(m.to())` (score döngüsündeki desen). ÇALIŞAN alpha ile
          (upfront todo filtresi değil): alpha yükseldikçe budama güçlenir; strictly
          daha güçlü. tt_move primi MVV-LVA sırasını bozabildiğinden güvenli
          `continue` (break değil). Tek sabit `kDeltaMargin`, yeni altyapı yok.
        - **Güvenlik:** promosyon muaf (vezir ekler, materyal sıçraması delta
          mantığına aykırı; SEE muafiyetiyle aynı) -> 8. sıra taktikleri korunur;
          çekte dal delta'ya hiç girmez (stand-pat yok, tüm kaçışlar aranır); marj
          (200 cp, bir piyondan biraz fazla) pozisyonel salınımı örter.
        - Kapılar: perft birebir (startpos d6 119060324, Kiwipete d5 193690690 ->
          movegen'e dokunmaz); 131/131 test (+2: DeltaPruningKeepsWinningCapture ->
          bedava vezir Rxe5 üst sınır alpha'yı aşar, budanmaz / KeepsMateSearch ->
          arka sıra matı, çekte dal). Düğüm sağlaması (Release, `a44d6eb` base, taze
          TT): startpos d13 668137->617518 (-%7.6), Kiwipete d12 715497->612182
          (-%14.4), bestmove makul (Kiwipete e2a6 aynı). Sağlama, kapı DEĞİL.
          kDeltaMargin=200 ilk elle-seçim, Blok 4/16 SPSA adayı.
      - [x] **countermove yumuşak history-bonusu (retry) — TAMAM, SPRT GEÇTİ H1
        (+10.4 ± 7.2 Elo, LLR 2.96 TAM KABUL, 4602 oyun, 1242-2256-1104, LOS %99.8).
        Base `94c5b95` vs new `aaaec37`. YENİ BASELINE `aaaec37`.** Eski sert-bant
        denemesi (`875d5f5`) -60 Elo regresyon vermişti; bu kez ayrı bant DEĞİL,
        quiet skoruna eklenip history bandına kırpılan yumuşak additive bonus ->
        ayrık-bant mimarisi korunur, LMP'nin iyi hamleleri budaması sorunu yok.
        cont_hist örtüşmesine rağmen (aynı [önceki taş][önceki hedef] indeksleme)
        net pozitif — mütevazı ama kesin, çok oyun istedi (delta pruning deseni).
        - **Mekanik:** `kCountermoveBonus=8192`; `countermove[12][SQUARE_NB]`
          (tek-slot recency, cont_hist gibi context-indexed -> aramalar arası
          kalıcı, age()'de temizlenmez, bayat/illegal slot yalnız üretilmiş legal
          hamlelerle karşılaştırıldığından zararsız). update_quiet_stats beta
          kesmesinde `countermove[pp][pt]=m`; score_move quiet dalında bu hamleye
          bonus (killer kontrolü önce return ettiğinden yalnız killer-olmayan
          quiet'lere). Yalnız move ordering; LMR/history-pruning ham stat'ı
          (history+cont) değişmedi (etki izole).
        - Sabit ÖLÇÜLEREK gerekçelendirildi (geçici enstrümantasyon, commit'te yok;
          hist-pruning-margin emsali): bonus quiet-skorlamaların ~%0.5-0.6'sında
          ateşliyor -> node başına en fazla bir eşleşme (bağlam başına tek CM) =>
          ~%10-18 düğümde CM-hit; no-op da dominant da değil. Bonus (8192) ölçekli
          history maks'ının (~5-10k) hemen üstünde -> CM güçlü quiet gibi sıralanır,
          killer altı. 2 test (CountermoveKeeps WinningTactic/MateSearch). Düğüm
          sağlaması (kapı DEĞİL, Release taze tablo): startpos d13 780013 (b1c3),
          Kiwipete d12 488482 (d5e6) — karışık yön, patlama yok (düğüm != Elo).
          Ertelenen: CM bonusunu LMR/history-pruning stat'ına katmak, age()'de
          temizleme varyantı.
      - [~] **kök hamle sıralaması (önceki iterasyon düğüm sayıları) — DENENDİ, RAFA
        KALDIRILDI (SPRT H0 yönünde, kullanıcı erken durdurdu: -16.7 ± 22.7 Elo, LLR
        -0.718, 480 oyun, 116-225-139, LOS %7.5). Commit `4612c5c`, revert `3ae1331`;
        `git diff aaaec37 -- src/search.cpp tests/search_test.cpp` boş -> baseline
        `aaaec37` DEĞİŞMEDİ, testler 138.** ID'de kök hamlelerini önceki tamamlanan
        iterasyonun alt-ağaç düğüm sayısına göre azalan sırala (en iyi hamle tt_move
        ile önde; rank tabanlı skor, kScoreTT altı bant). `Searcher.root_move_nodes`
        iterasyonlar arası kalıcı (Searcher ID döngüsünde yaşar), her TAM kök aramasında
        commit (aspiration fail-high kısmi ölçümü atlanır).
        - **PLAN VARSAYIMI ÇÜRÜTÜLDÜ:** "kökte budama yok -> yeniden sıralama skoru/best'i
          DEĞİŞTİRMEZ (yalnız düğüm verimi)" varsayımı YANLIŞ. Alt-ağaç TT/history/LMR
          yol-bağımlılığı sonucu değiştiriyor (IIR/capture-history emsali; sezgisel,
          exact değil). Ölçüldü: gate'siz startpos d13 düğüm **+%62** patladı (sığ
          derinlikte düğüm sayıları gürültü — depth 1 hamle başına ~birkaç düğüm).
        - **Derinlik kapısı (kullanıcı kararı):** `kRootNodeOrderMinDepth=6` eklendi ->
          sığ gürültü elendi, startpos d13 düz (-%0.3) ama Kiwipete d12 **+%13** (karışık,
          IIR emsali). SPRT yine de net negatif.
        - **DERS (düğüm != Elo + geri-besleme desenkronizasyonu):** motorun mevcut kök
          sıralaması (tt + MVV-LVA + history) zaten iyi ve TT/history durumu o sırayla
          SENKRON. Köke node-count sırası enjekte etmek bu geri-beslemeyi bozup derin
          ağacı verimsizleştiriyor; node-count hamle kalitesinin gürültülü vekili
          (transpozisyon yoğunluğu/TT-hit oranını da yansıtır). Döviz kuru burada
          UYGULANAMAZ (yalnız EXACT hızlanmalara; burada sonuç değişiyor). İLERİDE ADAY:
          Stockfish-tarzı önceki-iterasyon SKORU'na göre kök sıralaması (node-count değil),
          ya da yalnız zaman yönetiminde "easy move" tespiti için düğüm sayısı.

*Blok 4 — Texel tuning (kullanıcı kararı 2026-07-10: dahil, en sonda):*

- [x] **14. Veri pipeline'ı TAMAM (`5e64fd4`)**: gömülü `chess datagen <games> <out>
      [seed] [depth]` alt-komutu (cutechess+PGN yerine — kullanıcı kararı). Self-play,
      quiet-etiketli (FEN, oyun sonucu) veri. Çekirdek `datagen.hpp/cpp` engine
      kütüphanesinde -> NNUE veri toplamaya da hizmet eder. Quiet filtresi: çekte değil
      + bestmove yakalama/promosyon değil + skor mat değil; açılış plyleri atlanır.
      `tools/sprt/gen-data.ps1` paralel çok-işçi. 133 test.
- [~] **15. Texel fit: ALTYAPI TAMAM, İLK TUNING ~NÖTR (2026-07-12).** Baseline
      `94c5b95` DEĞİŞMEDİ (tune edilmiş eval adopte edilmedi).
      - `2742583` **15a (EXACT)**: tunable ağırlıklar constexpr -> mutable `g_eval`;
        EG PST MG'den ayrıldı; SEE+delta pruning kendi dondurulmuş MaterialValue'sunu
        tutar (material tune edilince arama budaması kaymasın). Düğüm birebir.
      - `29ddeaa` **15b/c (tuner)**: flat param haritası (kaydet/yükle + tuner tek
        doğruluk kaynağı), C++ `chess tune` (feature: material+PST doğrudan, gerisi
        finite-diff -> DRIFT YOK, testli; K kalibrasyonu; Adam). `3dfc3a9` EvalFile UCI
        option (aynı binary üzerinde izole tuned-eval SPRT'si).
      - `1343b74` **15d İKİ SPRT -> ~NÖTR**: (1) tune-tümü **-110 Elo REGRESYON** ->
        teşhis: eval ölçeği şişti (vezir 900->1185), aramanın cp-kalibre DONDURULMUŞ
        marjlarını (RFP/futility/razor/LMP/null/delta/aspiration) bozdu (tempo/improving
        emsali). (2) düzeltme (kullanıcı kararı): materyali DONDUR (ölçek çıpası) +
        PST/pozisyoneli varsayılana AdamW weight decay ile regularize -> **~NÖTR**
        (932 oyun, -9.3 ± 19.4, LOS %17). Material freeze -110'u düzeltti (ölçek
        hipotezi DOĞRULANDI) ama regularize pozisyonel tuning zaten iyi elle-ayarlı
        eval'i sığ self-play verisinde YENEMEDI.
      - İLERİDE: daha derin/çeşitli veri, joint arama-marj tuning (madde 16), farklı
        reg, ya da Faz 3 NNUE eval'i değiştireceğinden erteleme. Beklenti (+50-100) bu
        pass'te GERÇEKLEŞMEDİ.
- [ ] **16. (opsiyonel) Arama sabitleri mini-SPSA/elle**: aspiration delta (25),
      RFP 80×d / d≤6, futility {150,250,400} / d≤3, LMP 3+d² / d≤8, razor
      {300,500,700} / d≤3, LMR taban 0.75 / bölen 2.25 / kLmrStatDiv 512, history
      bonus depth²/scale 64 — hiçbiri SPRT ile ayarlanmadı, hepsi ilk elle seçim.

**Faz 2D tetikleyicisi: tüm bloklar (1-4) kapanınca** (kullanıcı kararı,
2026-07-10). Fork kısıtı geçerli kalır: 2D commit'leri NNUE reposuna N4'ten önce
cherry-pick edilmeli.

**Faz 2D — Multi-threading (Lazy SMP)**

**Tetikleyici: Faz 2C-devam'ın tüm blokları kapanınca (2026-07-10 kararı).**
Modern motorların standart, en sade etkili paralel yaklaşımı: N thread aynı kök
pozisyonu, paylaşılan tek bir TT üzerinden arar; TT ve küçük sıralama farklarıyla
doğal olarak ıraksarlar, ana thread raporlar. Gereksinimler:

- [x] **Thread-safe TT (lockless XOR)** — TAMAM, tek-thread EXACT, commit `19d4a04`.
      Her yuva iki 64-bit atomik söz (`Bucket`): key = gerçek anahtar XOR paketlenmiş
      data, data = score|eval|move|depth|gen_bound (8 bayt, B1'de hazırlanan geometri).
      Yırtık okuma (k^d)!=key -> miss. İç depolama `unique_ptr<Bucket[]>` (atomik
      kopyalanamaz), public `TTEntry` probe çıktısı sabit -> `search.cpp` HİÇ değişmedi.
      Kapı EXACT (SPRT değil): 141 test + düğüm eşitliği (startpos d13 780013, Kiwipete
      d12 488482, `aaaec37` ile birebir).
- [x] **Thread pool + per-thread Searcher + orchestrator** — TAMAM, threads=1 EXACT,
      commit `a967672`. ID döngüsü `run_id_loop`'a çıkarıldı (is_main bayrağı: yalnız
      ana thread info raporlar). Yeni `search_iterative` overload'u (`vector<
      SearchTables*>`): her thread kendi tablosu + Searcher; ana thread çağıran
      thread'te, yardımcılar `std::thread`'de. Durma: ana bitince `smp_stop` -> yardımcı
      abort; yardımcılar deadline'a da uyar. Toplam düğüm join sonrası (yarışsız).
      threads<=1 -> tek-thread yola delege (birebir).
- [x] **`Threads` UCI option** (spin, min 1, max `hardware_concurrency`) — TAMAM
      (`a967672`). `g_thread_tables` havuzu (thread başına SearchTables, oyun boyu yaşar,
      ucinewgame'de hepsi clear). handle_setoption parse+clamp+havuz büyüt.
- [x] **Determinizm kaybı** kabul edildi: çok-thread arama tekrarlanabilir değil ->
      düğüm/skor iddialı testler tek-thread; yeni SMP testi (uci) yalnız legallik+mat.
- [x] **SPRT altyapısı hazır** (`e810a9a`): tools/sprt per-engine `-NewThreads`/
      `-BaseThreads` (option.Threads) + oversubscription uyarısı + GUI alanları.
- [x] **SCALING SPRT — GEÇTİ H1 (2 thread vs 1 thread, aynı kod)**: Elo **+88.7 ±
      24.5**, LOS %100, LLR 2.95 TAM KABUL (424 oyun, W-D-L 168-194-62). SMP gerçekten
      Elo veriyor (nps ölçeklemesi ≠ Elo idi; ölçüldü, doğrulandı). **FAZ 2D TAMAM.**
      **2→4 thread ölçekleme de GEÇTİ H1: Elo +75.5 ± 22.3, LOS %100, LLR 2.95 TAM
      KABUL (505 oyun, 190-233-82).** Ölçekleme sürüyor (1→2 +88.7, 2→4 +75.5; azalan
      getiri beklenen/sağlıklı, kümülatif 1→4 ~+164 Elo mertebesi).

- [x] **SMP iyileştirme paketi (2026-07-15): TT prefetch + 4-yollu bucket KABUL,
      depth-skipping RAFA. YENİ BASELINE `1f497dc`.** Üç aday tek pakette denendi,
      ikili SPRT ayrıştırdı:
      - `01853a0` **TT prefetch** (`_mm_prefetch`, EXACT): çocuk düğümün TT cluster'ı
        do_move sonrası çektirilir (negamax + qsearch). Davranış-koruyan (startpos d13
        780013 / Kiwipete d12 488482 birebir kanıtlandı). ~%1-3 nps.
      - `ee266e5` **4-yollu set-associative TT** (heuristik): 1 yuva/index → 4 yuvalı
        cluster (Bucket 16B ×4 = 64B = tam cache line). probe 4 yuva tarar; store
        aynı-key güncelle (depth-preferred) ya da victim `depth - kReplaceAgePenalty
        (8)*yaş` en düşük. Lockless-XOR yuva-içi korunur. Tek-thread node-equality
        BİLİNÇLE kalktı (heuristik); doğruluk: perft birebir + `FourWayAssociativity`
        testi + sağlama (d13 609707 e2e4, TT retention iyileşti).
      - `466b6f6` **depth-skipping** (Stockfish SkipSize/SkipPhase, yardımcı staggering)
        → **RAFA (`1f497dc` revert)**. Tek-thread'te INERT (thread_idx daima 0, EXACT).
      - **İKİLİ SPRT (ders):** paket 1v1'de **H1 (pozitif)**, 4v4'te **NÖTR**. depth-skip
        1 thread'te inert olduğundan **1v1 H1 tamamen prefetch+4-way'e ait** → o ikisi
        kanıtlandı, kabul. depth-skip yalnız çok-thread'te etkir; orada paket nötr ve
        depth-skip tek başına da (466b6f6) daha önce 4v4'te düzdü (+3.5 ± 48.9) →
        hiçbir thread sayısında kanıtlanmış kazanç yok, ölü ağırlık + fork yükü →
        kaldırıldı. **DERS: 1-thread kazancı N-thread'te yıkanabilir (motor zaten
        güçlü + SMP gürültüsü); "nötr" ≠ regresyon. Bir bileşen bir thread sayısında
        inert ise, o koşuyu diğer bileşenlere ATFETMEK için kullan (paketi ayrıştırır).**
      - 142 test (LazySmpSkip çıktı, FourWayAssociativity kaldı). Fork: prefetch+4-way
        N4'ten önce NNUE'ya cherry-pick edilecek. İleride yüksek çekirdek (8/12/14)
        için depth-skip yeniden aday (kolay re-add).

Kalan opsiyonel ileri: TT prefetch mesafesi/derinliği ince ayarı, `kReplaceAgePenalty`
tuning (Blok 4/16), yüksek-çekirdek depth-skip retry; **ilk sıradaki iş Faz 3 (klasik)
— Eval Güçlendirme (Blok E1'den).** (NNUE cherry-pick borcu KAPANDI; NNUE ayrı repoda.)

### Faz 3 (klasik) — Statik Değerlendirme (Eval) Güçlendirme

**NNUE bu repoda YOK.** NNUE işi ayrı repoda (`../ChessEngineNNUE`, Faz 3 orada).
Klasik reponun Faz 2D sonrası yeni odağı: statik değerlendirmeyi güçlendirmek.
Gerekçe: klasik tarafta kalan en büyük **ölçülmüş** kaldıraç eval — her katsayı
elle-seçilmiş `constexpr`; geçmiş SPRT'ler yalnız "bu terim var olmalı mı?"
sorusunu cevapladı, "katsayı doğru mu / hangi terimler eksik?" değil. Standart
klasik motora göre eksik terimler: threats/hanging, passed-pawn rafineleri,
backward/connected/phalanx piyon, outpost, bad bishop, rook-on-7th, king-safety
rafineleri, endgame scaling, tempo (+ pawn hash yok, pawn eval her çağrıda yeniden).

**KRİTİK KISIT (her blokta): eval-ölçek ↔ arama-marjı bağı.** RFP/futility/razor/
LMP/null/delta/aspiration marjları cp-kalibre ve DONDURULMUŞ; SEE+delta ayrı
dondurulmuş `MaterialValue` kullanır. Genel eval ölçeğini kaydıran değişiklik bu
marjları bozar — kanıtlı emsaller: tempo `evaluate()` içinde −18.6, improving nötr,
tune-all −110. Kural: yeni terimleri **mütevazı** tut, ölçeği kaydırma; büyük etki
gerekiyorsa joint arama-marj tuning'e (E7) bırak.

**EN KRİTİK DERS — ORTOGONAL vs YENİDEN-İFADE (2026-07-16, 10+ veri noktası).** Motor
artık güçlü; marjinal bir terimin SPRT'yi geçmesi için **YENİ bilgi** taşıması şart.
İki sınıf net ayrışıyor:
- **GEÇEN** (hep): daha önce hiç sayılmayan **ortogonal** sinyal. tapered, pawn
  structure, mobility, bishop pair, rook-file, king safety (İLK kurulum), threats/
  hanging + arama terimleri (singular, SEE, IIR...). Hepsi eksik bir eksene bilgi ekledi.
- **KALAN/H0** (hep): (a) **zaten yakalanan sinyalin yeniden-ifadesi** — check extension
  (arama zaten görüyor), improving, capture history (MVV-LVA+SEE zaten güçlü), kök
  node-sıralaması; ya da (b) **dondurulmuş kalibre alt-sisteme enjeksiyon** — tempo /
  tune-all (cp marjları), **king-on-open-file** (−11.9; king exposure zaten king-PST +
  kalkan + ring ile ÜÇ katmanda) ve **piyon king-ring** (~nötr; frozen nonlinear
  SafetyTable'a `KingAttackWeight[PAWN]` enjeksiyonu, tablo units-tanımıyla kalibre).
Kural: yeni terim önerince ÖNCE sor — "bu sinyal şu an başka bir yerde sayılıyor mu?
kalibre-dondurulmuş bir sisteme mi giriyor?" İkisinden biri EVET ise, ayrı bir SPRT'de
denemek yerine ya ORTOGONAL bir terime geç ya da o alt-sistemi joint tune et (E7).
Emsal-genişletme: H0 çıkan bir terimi "sabiti düzeltip" retry etme (capture-history:
node-pozitif yapıldı, Elo yine −16 — sorun sabit değil KATEGORİ).

**DERSİN RAFİNESİ (2026-07-17, protected passer −22.1'den): sinyali ADIYLA değil
SONUCUYLA ara.** Protected passer'da kategori testi uygulandı ve "korunan geçer piyonu
bilen bir terim yok" diye GEÇTİ — cevap doğruydu ama SORU yanlıştı. Doğru soru: **"bu
özelliğin SONUCU zaten fiyatlanmış mı?"** Korunan geçer piyon inşa gereği asla izole
değildir -> izole terimi o yapıyı zaten +30 EG ödüllendiriyordu; terim bunu %60 büyüttü
ve −22 verdi. **Korelasyonlu vekil terimler de "zaten sayılıyor" demektir**; farklı
isimli bir terim aynı sinyali taşıyor olabilir. Yeni terimde: aynı yapıya sahip iki
pozisyonu elle eval'leyip mevcut eval'in onları ZATEN ne kadar ayırdığını hesapla —
ayrım zaten büyükse terim yeniden-ifadedir.
**İKİ KAPI DA GEREKLİ AMA HİÇBİRİ YETERLİ DEĞİL:** (a) enstrümantasyon (ateşleme/
büyüklük) ORTOGONALLİĞE KÖRDÜR — protected passer'da "SAĞLIKLI" dedi (%20 ateşleme,
2.14 cp) ve terim −22 verdi; (b) ağırlık=0 düğüm-eşitliği yalnız YAN ETKİ yokluğunu
kanıtlar, terimin DOĞRU şeyi ölçtüğünü değil. **ANOMALİ SİNYALİ: ortalama katkısı ~2 cp
olan bir terim −22 Elo veremez — veriyorsa sinyal zaten sayılıyordur.** Bu oran (etki
büyüklüğü vs Elo hasarı) tutarsızsa kategoriyi yeniden sorgula.

**ÜÇÜNCÜ HATA SINIFI (2026-07-17, blockade −12.4'ten): İŞARET-TUTARSIZ PREDICATE.**
Ortogonal olmak YETMİYOR. Blockade kategori testini (adıyla + sonucuyla) GEÇTİ — sinyal
gerçekten hiçbir yerde fiyatlanmamıştı — ve yine de −12.4 verdi, çünkü predicate **zıt
işaretli iki alt-kümeyi tek sabitle** cezalandırıyordu (at blokajı piyon sahibinin
aleyhine, kale blokajı LEHİNE: kale pasifleşip bağlanır). **Yeni terimde ÜÇÜNCÜ soru:
"bu predicate'in ateşlediği TÜM durumlarda etkinin İŞARETİ aynı mı?"** Değilse tek sabit
doğru olamaz. Pratik kapı: enstrümantasyona **alt-küme kırılımı** koy (blockade'de
blokajcı taş türü) — ateşleme oranı ve büyüklük bunu göstermez, kırılım gösterir.
**Kırılımı SPRT'den ÖNCE ölç ve riski ÖN-KAYIT ET:** blockade'de "%84 kale/vezir, işaret
orada şüpheli" commit mesajına önceden yazıldı -> H0 gelince teşhis hazırdı, sonradan
hikâye uydurulmadı. **Bonus: kırılım varyantı da eleyebilir** — blockade'de "minör-only"
gate'i mevcut sayılardan hesaplandı (%1.36 MG / %0.03 EG -> no-op) ve EK SPRT'siz elendi.

**Metodoloji (mevcut disiplin): her terim ayrı commit + ayrı SPRT.** Kabul kapısı
Elo; düğüm sayısı Elo proxy'si DEĞİL (Blok 2/6 dersi). Bağımsız sezgiselleri tek
SPRT'de PAKETLEME (Blok 1/3 dersi). Mütevazı terim = çok oyun ister (razoring/LMP
deseni). **Mütevazı etkide erken kareye göre karar VERME** (LMP dersi): 135 oyun /
±47 hata bandı bilgi taşımaz; SPRT'yi sınıra bırak ya da sabit-N kullan. SPRT sonucunu
**kapatmadan** final satırıyla kaydet (threats istatistiği test kapanınca kayboldu).
Beklentiler mertebe tahmini, söz değil ("+15-40" gibi lore beklentileri ÖLÇÜM değil).

**AÇILIŞ KİTABI DEĞİŞTİ (2026-07-23, kullanıcı kararı): 22 pozisyon -> 7314 pozisyon
(`tools/sprt/noob_2moves.epd`, 2-hamlelik açılışlar).** Varsayılan artık bu; eski
`book.epd` yalnız `-Book` ile seçilebilir. **KARŞILAŞTIRMA UYARISI: `8ada3d8` ve
öncesindeki TÜM kayıtlı Elo sayıları 22-pozisyonluk kitapla ölçüldü.** Kitap
genişleyince oyunlar arası korelasyon düşer, açılış çeşitliliği ve beraberlik oranı
değişir -> yeni koşuların mutlak Elo'ları eskilerle BİREBİR kıyaslanamaz (aynı terim
iki kitapta farklı sayı verebilir). Bir terimi eski bir sonuçla kıyaslarken bunu
belirt; SPRT kararının kendisi (H0/H1) etkilenmez, yalnız ölçek/varyans kayar.
Doğrulama (kitap değişiminde yapıldı): 7314/7314 FEN motorca parse edildi + hepsi
legal hamle verdi + tekrar yok; cutechess duman testinde oyunlar gerçekten yeni
kitaptan başladı (PGN'deki FEN etiketleri yeni kitapta VAR, eskisinde YOK — cutechess
sayaç alanlarını `0 1`'e normalize ettiğinden karşılaştırma ilk 4 FEN alanı üzerinden).

**Yeni terim eklemenin standart iş akışı** (E2-E6 her maddesi): (1) `EvalParams`
struct'a alan (`eval.hpp`) + `make_default_eval_params` varsayılanı (`eval.cpp`) +
`flat_param_pointers`/`flat_param_names` sırası (`eval_params.cpp`, frozen sınırının
doğru tarafında) -> terim otomatik tunable + kaydedilebilir olur; (2) izole test
edilebilir `void term(const Board&, int& mg, int& eg)` helper, `eval_accumulate`
içinde çağır; (3) `tests/eval_test.cpp` deseninde test (izole, renk-simetri, faz);
(4) build + testler + elle sanity; (5) GUI'den SPRT, H1 -> tut + yeni baseline,
H0/nötr -> geri al + dersi buraya yaz.

*Blok E1 — Eval altyapısı + hız (prereq): TAMAM (YENİ BASELINE `5dbc2ff`, 148 test).*
- [x] **Pawn hash table (TAMAM, EXACT, commit `201e9d8`).** Global lockless-XOR pawn
      cache (TT Bucket deseni; küme/aging/paketleme yok). `Board::pawn_key` incremental
      (put/remove_piece'te `if pt==PAWN` XOR; do_move değişmedi; `compute_pawn_key`
      oracle; pawn_key = key'in piyona-özel alt-kümesi, side/rok/ep DAHİL DEĞİL).
      `PawnTable` (pawn_table.hpp/cpp) 2^16 yuva ~1MB. `eval_accumulate` probe/store;
      `pawn_structure` saf/raw kaldı. `g_pawn_cache_enabled` bayrağı -> TUNER
      finite-diff'te KAPATIR (perturbe pawn ağırlığı için bayat değer -> gradyan
      bozulurdu). Temizleme: ucinewgame + Clear Hash + EvalFile. Kapı EXACT:
      node-equality vs `1f497dc` birebir (startpos d13 609707/cp28/e2e4, Kiwipete d12
      549629/cp-72/e2a6), perft birebir, 148 test (+6 PawnTable, walk_and_check'e
      pawn_key invaryantı). nps +%2.0/+%5.1. Değer E3'te katlanır. NNUE cherry-pick ADAYI.
- [x] **Birleşik attack-pass (TAMAM, EXACT, commit `5dbc2ff`).** mobility() ve
      king_safety() aynı N/B/R/Q atak setlerini iki kez üretiyordu; tek geçişli
      `mobility_king_safety_impl` (anon ns, `AttackEval {mob_mg,mob_eg,ks_mg}`) her
      atak setini bir kez hesaplar -> sliding-magic çağrıları ~yarıya. mobility()/
      king_safety() ince sarmalayıcı (impl'e delege -> izolasyon testleri değişmedi,
      drift yok); eval_accumulate impl'i bir kez çağırır (int toplam, sıra bağımsız ->
      ayrı çağırmakla BİREBİR). Kapı EXACT: node-equality vs `201e9d8` birebir
      (609707/549629), perft trivial (movegen'e dokunmadı). nps +%2.4/+%2.2. Threats
      (E2) aynı pass'e eklenecek.

*Blok E2 — Yüksek-değer pozisyonel terimler:*
- [x] **Threats / hanging pieces (tam aile) — TAMAM, SPRT H1 KABUL (commit `9c4f6d1`).
      YENİ BASELINE `9c4f6d1`, 152 test.** Dört alt-terim (BEYAZ−SİYAH, tapered),
      E1'in birleşik attack-pass'ine FOLD edildi (ayrı geçiş = E1 kazancını geri verir):
      `mobility_king_safety_impl` -> `attack_eval_impl`; iç döngüde tür-bazlı aggregate
      atak bitboard'ları (`by_pawn` shift ile, `by_minor`=at|fil, `by_rook`, `by_piece`),
      iki renk döngüsünden sonra `by_all=by_pawn|by_piece|king_attacks` ile threats.
      - **ThreatByPawn** (40/30): piyon vuruşu altındaki rakip N/B/R/Q
      - **ThreatByMinor** (25/20): minör atağı altındaki rakip R/Q (minör→majör)
      - **ThreatByRook** (20/15): kale atağı altındaki rakip Q (kale→vezir)
      - **Hanging** (25/25): bizim vurduğumuz + rakibin savunmadığı rakip N/B/R/Q
      Heuristik -> kabul kapısı SPRT (node-equality DEĞİL). Ölçek modest tutuldu
      (eval-ölçek↔arama-marjı bağı; tempo −18.6 / tune-all −110 emsali). 8 skaler
      EvalParams'a frozen sınır ÖNÜNE eklendi (tunable); `eval_frozen_start` 812->820.
      Sabitler ilk elle-seçim (E7 SPSA/Texel adayı). `threats()` izole wrapper (mobility/
      king_safety deseni). Perft birebir (movegen'e dokunmaz). 4 test (148->152:
      ThreatByPawnBonus, ThreatHangingPiece [savunulunca kalkar], ThreatByMinorOnMajor,
      ThreatsSymmetry); FrozenBoundary son-tunable "hanging_eg"e güncellendi.
      **SPRT: base `5dbc2ff` vs new `9c4f6d1`. 1-thread H1 TAM KABUL (kanonik kapı;
      tüm baseline'lar 1-thread ile kuruldu); 2-thread + büyük Hash ile de H1 doğrulandı.
      Tam istatistik (Elo±hata/W-D-L/oyun) kaydedilmedi — test kapatıldı.** NNUE
      cherry-pick ADAYI. **DERS (SMP + SPRT metodolojisi): İLK 4-thread koşusu H0 verdi
      -> teşhis: kod bug'ı DEĞİL (SMP entegrasyonu satır satır incelendi, tam thread
      izolasyonu doğrulandı; bkz. memory `sprt-gui`) -> OVERSUBSCRIPTION (2 motor × 4
      thread > 14 fiziksel çekirdek, aynı eşzamanlılıkta açlık -> sığ arama -> eval
      sinyali boğuldu). Thread↑ -> SPRT eşzamanlılığı↓ (2 motor × N thread ≤ fiziksel
      çekirdek) + Hash'i thread'e göre büyüt. "1-thread kazancı N-thread'te yıkanabilir"
      dersinin (SMP paketi) TERSİ hata: yanlış kurulum gerçek kazancı gizler.**
- [~] **King safety rafineleri — E7 JOINT TUNING'E ERTELENDİ (2026-07-16, kategori
      kanıtı aleyhte).** İki deneme üst üste başarısız (aşağıda): açık-hat net H0,
      piyon-ring ~nötr. Kök sebep tekil sabit değil KATEGORİ — hepsi ya (a) king-PST +
      kalkan + ring ile zaten ÜÇ katmanda sayılan king-exposure'ı yeniden ifade ediyor,
      ya (b) units-tanımıyla kalibre **dondurulmuş nonlinear SafetyTable**'a enjeksiyon.
      Kalan adaylar (pawn storm, safe/knight check, attacker-count çarpanı) da AYNI
      frozen sisteme girer -> tek tek elle-SPRT YERİNE **E7'de safety bloğunu (weights +
      SafetyTable) BİRLİKTE tune et** (king-safety'den değer almanın dürüst yolu).
      Tek tek yeniden deneme (ör. ağırlık-2 retry) YOK (bkz. EN KRİTİK DERS + capture-
      history emsali). İki deneme kaydı:
      - [~] **ring'e piyon saldırısı — DENENDİ (`3fc3d73`), RAFA (SPRT ~nötr/H0), revert
        `cce222c` (`9c4f6d1`'e birebir).** `KingAttackWeight[PAWN]` 0->1; piyon vuruşları
        (`by_pawn`) rakip şah halkasında units'e eklenir (nonlinear tabloya girer -> önceki
        üç hatayı bilinçle atlatmıştı: ortogonal sinyal, doğal gate, tablo doygunluğu).
        **SPRT base `9c4f6d1` vs new `3fc3d73`: H1'e gitmedi -> 639 oyun, W-D-L 170-284-185,
        Elo −7.3 ± 20.3, LOS %24.1, LLR −0.454 (kullanıcı durdurdu).** Üç hatayı düzeltmek
        bile yetmedi -> asıl sorun: frozen SafetyTable units-tanımıyla (N2/B2/R3/Q5, piyon
        YOK) kalibre; `KingAttackWeight[PAWN]` enjeksiyonu tablonun çalışma noktasını
        global kaydırıyor (tempo/tune-all deseni, ama frozen safety içinde). -> E7 joint.
      - [~] **şah açık/yarı-açık hatta — DENENDİ (`9c62fe6`), RAFA (SPRT H0), revert
        `d7963d2` (`9c4f6d1`'e birebir).** Rakipte R/Q varken şah hattı/komşu sütun
        dost piyonsuzsa danger (open 15 / semi 8), SafetyTable'dan SONRA düz eklenir.
        **SPRT: 1533 oyun, W-D-L 378-725-430, Elo −11.9 ± 12.7, LOS %3.3, LLR −1.72
        (H0'a net, kullanıcı durdurdu).** TEŞHİS (üç kök sebep): (1) **üst-üste sayım**
        — king exposure zaten king-PST (MG'de açık şah ağır cezalı) + kalkan + ring
        atağı ile ÜÇ yerde yakalanıyor; açık-hat dördüncü kez şişirdi. (2) **kaba gate**
        — "rakipte R/Q var" orta oyunda hep doğru -> terim çok geniş ateşliyor (güvenli
        yarı-açık sütunlar da cezalanıyor; kale/vezir o hatta BAKIYOR mu bakmıyor).
        (3) **doğrusal ekleme SafetyTable'dan SONRA -> nonlinear doygunluğu ATLIYOR**;
        düşük-tehlike pozisyonlarında düz −15/−8 saf gürültü. Emsal: check extension /
        improving / capture history (güçlü motorda zaten yakalanan sinyali kaba biçimde
        tekrar eklemek net-negatif). İLERİDE ADAY: açık-hat'ı SafetyTable UNITS'ine kat
        (doygunlukla birleşsin), gate'i "R/Q o hatta bakıyor" yap, kalkanla örtüşmeyi
        azalt; ya da E7 joint king-safety tuning'e bırak.
- [ ] **Mobility quality**: mobility area'dan rakip piyon vuruşlarını çıkar
      (`by_pawn[enemy]` birleşik pass'te zaten var). ORTA GÜVEN — mevcut mobility
      terimini RAFİNE eder (ortogonal yeni sinyal değil), ama rakip-piyon-kontrolü
      şu an hiçbir yerde sayılmadığından tam "yeniden-ifade" de değil. Beklenti +5-15.

**SIRADAKİ İŞ (2026-07-16 pivot): E3/E4'ün ORTOGONAL terimleri** (passed-pawn
rafineleri, outpost, rook-on-7th) — kategori kanıtı (EN KRİTİK DERS) bunları
king-safety rafinelerinin ÖNÜNE koyuyor: yeni eksene bilgi ekliyorlar, dondurulmuş
alt-sisteme enjeksiyon değiller. Mobility quality (orta güven) ve E7 king-safety
joint tuning sonraya.
**DURUM (2026-07-17): E3 rook-behind-passer (kale geçer piyon arkasında) KABUL -> YENİ
BASELINE `167ade2`, 167 test.** SPRT base `8b60653` vs new `167ade2`: 7741 oyun, Elo +7.5
± 5.7, LOS %99.5, LLR 2.97 (2.94'ü GEÇTİ) TAM KABUL — E3'ün İKİNCİ geçen terimi. +7.5 >
elo1(5) olduğundan escort gibi tek başına certify etti (bundle'a gerek yok). EG-only, yalnız
kendi kale, `between_bb` temiz-hat, göreli sıra≥3, ağırlık 20; escort'un pawn-cache passer
kümesini kullandı (sıfır ek altyapı). Üç soru da geçti (ortogonal / sonucuyla sayılmıyor /
işaret-tutarlı). Kapılar: perft birebir, ağırlık=0 düğüm-eşitliği (escort+rook 0 -> startpos
d13 1121128/cp27 = `47fada6`, sıfır yan-etki), enstrümantasyon EG %8.7-42.8 / MG ~0.
YAN BULGU: `PawnTable.CachedEqualsRaw`'daki şahsız FEN (`8/pp4pp/8/8/8/8/1P4P1/8`) baseline'da
da vardı -> Debug'da `king_square()` array OOB (Release sessiz); şah eklenerek düzeltildi
(ayrı commit `19b728c`, test-only). Öncesi: şah eskortu KABUL (`8b60653`, +6.3 ± 5.0). Şimdi
5 E3/E4 denemesinden 3 pozitif (escort + rook-behind tam-kabul, outpost koşullu), 2 red
(protected passer −22.1, blockade −12.4).
DURUM (2026-07-23, GÜNCEL): **E3 + E4 BİTTİ.** E3/E4 bundle certified (`2122456`), sonra E4
kale-7.-sıra (gated) KABUL -> **BASELINE `8ada3d8`** (+7 ± 6.6, LOS %98.2, LLR 2.0, kullanıcı
erken durdurdu; +7 > elo1 -> tek başına certify). **E4-KALANI ÖLÇÜMLE KAPANDI (SPRT
HARCANMADAN):** pigs-on-7th ~918K eval'de SIFIR ateşleme -> ölü; "rakip-kale-7." aslında
rook-behind-passer'ın rakip yarısıydı (rook_on_seventh zaten simetrik) ve ön-taramada elendi
(%100 rook_semi örtüşmesi + kardeş terimin 1/10'u büyüklük); rank-ölçekli -> E7; bishop-pair
zıt-kare + trapped-rook -> atlandı. Ayrıntı + yeni "KIYAS ÖLÇÜMÜ" metodolojisi: Blok E4
"E4-KALANI KAPANDI" girişi. **AKTİF: BLOK E5 — endgame scaling.** Commit 1 (scaling
altyapısı + zıt-renk fil/OCB) KOD TAMAM -> `c40dfb9`, 185 test, **SPRT BEKLİYOR**
(base `8ada3d8`, 1-thread, 5+0.05, elo0=0/elo1=5). Ön-tarama sıralamayı belirledi:
OCB %5.23 (yapıldı) > piyonsuz-küçük-fark %1.48 (sıradaki ayrı commit) > yanlış
kale-piyonu %0.000 (ÖLÜ). NNUE cherry-pick ADAYI.
**YENİ TERİMDE ÜÇ SORUYU DA SOR (bu blokta üçü de ayrı ayrı ısırdı):** (1) sinyal ADIYLA
sayılıyor mu? (2) SONUCU zaten fiyatlanmış mı — korelasyonlu vekil var mı (protected
passer: izole terimi)? (3) predicate'in ateşlediği TÜM durumlarda etkinin İŞARETİ aynı mı
(blockade: at vs kale blokajı zıt)? **UYARI: "connected/phalanx pawns" (2) tuzağına neredeyse
kesin düşer** — bağlı piyonlar da inşa gereği izole değildir, yani izole terimi onları da
zaten fiyatlıyor (protected passer'ın birebir aynısı). "Backward pawns" daha umutlu ama
aynı hesabı ÖNCE yap. **Rook-behind-passer'da (3)'e dikkat**: kendi passer'ının arkasındaki
kale ile rakip passer'ının arkasındaki kale aynı işarette mi, ayrı ayrı düşün.

*Blok E3 — Piyon yapısı derinleştirme (E1 pawn hash sonrası):*
- [~] **Korunan geçer piyon (protected passer) — DENENDİ, RAFA KALDIRILDI (SPRT H0 TAM
      RED). `5e302b6` -> revert `f0a1c2d`; eval kaynakları `47fada6`'ya BİREBİR
      (`git diff 47fada6 -- include/engine/eval.hpp src/eval.cpp src/eval_params.cpp
      tests/eval_test.cpp` boş; düğüm birebir: startpos d13 = 1121128, cp 27, PV özdeş).**
      **SPRT base `47fada6` vs new `5e302b6`: 1621 oyun, W-D-L 402-714-505, Elo −22.1 ±
      12.6, LOS %0, LLR −2.96 TAM RED.** Dost piyonun vurduğu karedeki geçer piyona düz
      bonus (10/18). Piyon-saf -> pawn cache'e girdi, sıfır altyapı.
      - **KÖK SEBEP: YENİDEN-İFADE (kategori), sabit değil. Kanıt İNŞA GEREĞİ, ölçüm
        değil: korunan geçer piyon TANIMI GEREĞİ ASLA İZOLE DEĞİLDİR** — koruyucu komşu
        sütunda olmak zorunda (d5'i koruyan c4), yani `wp & AdjacentFileMask[file]` daima
        dolu -> izole cezası zaten uygulanmıyor. Üstelik koruyucu piyonun kendisi de
        genelde geçer -> kendi sıra bonusunu da alıyor. Sayıyla (EG, aynı iki piyon):
        c4+d5 **zinciri** = passed[3] 35 + passed[4] 60 + izole cezası YOK = **95**;
        a4+d5 **ayrık** = 35 + 60 − 15 − 15 = **65**. Yani eval "piyonlar birbirini
        destekliyor" yapısını **zaten +30 EG ödüllendiriyordu** (izole terimi üzerinden);
        +18 EG eklemek bu farkı %60 büyüttü, SIFIR yeni bilgiyle.
      - **KATEGORİ TESTİ FAZLA DAR UYGULANDI (asıl ders, EN KRİTİK DERS'e işlendi):**
        "korunan geçer piyonu bilen bir terim var mı?" diye soruldu, cevap doğru olarak
        "yok"tu. Sorulması gereken: **"korunan olmanın SONUCU zaten fiyatlanmış mı?"**
        Sinyal ADIYLA arandı, SONUCUYLA değil -> korelasyonlu vekil terim (izole) gözden
        kaçtı. Bu, ölçüm hatası değil muhakeme hatası.
      - **ENSTRÜMANTASYON KAPISI ORTOGONALLİĞE KÖR (gate tasarımı dersi):** ateşleme
        oranı + büyüklük "no-op mu / dominant mı?" sorusunu cevaplar, "bu sinyal zaten
        sayılıyor mu?" sorusunu CEVAPLAYAMAZ. Ölçüm "SAĞLIKLI" dedi (oyun sonu: %20
        ateşleme, abs/çağrı 2.14 cp, sum/çağrı 0.79 cp; orta oyun: %3.70 / 0.38 cp) ve
        terim yine −22 verdi. **Anomali erken uyarıydı ve kaçırıldı: ortalama katkısı
        ~2 cp olan bir terim −22 Elo veremez — meğer sinyal zaten sayılıyor olmasın.**
        Kapı gerekli ama YETERLİ DEĞİL; kategori muhakemesinin yerine geçmez.
      - **İkincil katkı:** terim, İLERLEYİNCE YOK OLAN statik bir yapıyı ödüllendiriyor
        (d5->d6 gidince c4 artık korumuyor -> −18) -> geçer piyonu itmeyi caydırıyor,
        hem de en çok ateşlediği fazda (oyun sonu), yani itmenin oyun kazandığı yerde.
      - **SABİT DÜZELTİP RETRY YOK** (kategori kuralı; capture-history emsali).
        Ölçüm karakteri kayda değer: korunan/geçer oranı fazlar arası SABİT (%10.4 orta
        oyun / %11.8 oyun sonu) — değişen şey geçer piyon SAYISI (0.38 -> 1.81/çağrı).
      - **KORUNDU (latent, terime bağlı değil):** `tests/pawn_table_test.cpp`
        **`SamePawnsDifferentPiecesNotStale`** — pawn_key yalnız PİYONları içerir, aynı
        piyonlu iki FARKLI pozisyon AYNI cache girişini paylaşır; bu test piyon-DIŞI bir
        terimin `pawn_structure`'a sızmasını (aşağıdaki TUZAK) yakalar. Mevcut
        `CachedEqualsRaw` yakalayamaz (her FEN'den önce cache'i temizler -> giriş
        paylaşımı hiç test edilmiyor). 157 -> 158 test. TUZAK (yaşandı): ilk FEN seçimi
        (at b6 vs g6) BOŞ GEÇİYORDU — at PST'si sol-sağ simetrik, iki pozisyon birebir
        aynı değerleniyordu (−13 vs −13); `ASSERT_NE` anti-vacuity guard'ı yakaladı,
        kareler KÖŞE (h8) vs MERKEZ (d4) yapıldı.
- [~] **Geçer piyon blokajı (blockade) — DENENDİ, RAFA KALDIRILDI (SPRT H0 TAM RED).**
      `af26f8f` -> revert; eval davranışı `47fada6`'ya BİREBİR (düğüm startpos d13 =
      1121128, cp 27, PV özdeş — EvalFile'sız). **SPRT base `47fada6` vs new `af26f8f`:
      2544 oyun, W-D-L 633-1187-724, Elo −12.4 ± 9.8, LOS %0.7, LLR −2.95 TAM RED.**
      Geçer piyonun durak karesinde rakip taş varsa düz ceza (10/20).
      - **YENİ HATA SINIFI: İŞARET-TUTARSIZ PREDICATE** (yeniden-ifade DEĞİL!). Kategori
        testi bu kez DOĞRU uygulandı ve terim GEÇTİ: sinyal ne adıyla ne sonucuyla
        fiyatlanmış — piyon mobilitesi eval'de HİÇ YOK, blokajcının PST'si önündeki
        piyondan habersiz (at d6'da bonusu piyon olsa da olmasa da alır), outpost sıra
        kapısıyla (4-6) örtüşmez (blokaj karesi rakip için göreli sıra 2-3). Sorun:
        **predicate zıt işaretli iki alt-kümeyi tek sabitle cezalandırıyor.**
        (a) at/fil blokajı -> piyon sahibinin AÇIKÇA aleyhine (minör blokaj karesinde
            mutlu, kovulamaz); (b) kale/vezir blokajı -> muhtemelen LEHİNE (piyon durur
            ama kale pasifleşir ve piyona bağlanır; Tarrasch: kaleler geçer piyonun
            ARKASINA aittir, önüne değil).
      - **ÖN-KAYITLI TEŞHİS DOĞRULANDI (metodoloji kazancı):** bu risk SPRT'den ÖNCE
        commit mesajına yazılmıştı. Enstrümantasyon blokajcı tür dağılımını ölçmüştü:
        orta oyunda minör baskın (6022 vs kale/vezir 4837), **oyun sonunda %84 kale/vezir
        (9972 vs minör 29)** -> terim en çok ateşlediği fazda çoğunlukla YANLIŞ işareti
        tam ağırlıkla uyguluyordu. Başarısızlık modunu testten önce yazmak, sonradan
        hikâye uydurmayı engelledi.
      - **VARYANT DENENMEDİ, ÇÜNKÜ ÖLÇÜM ONU DA ELEDİ (ek SPRT'siz):** "minör-only"
        gate'i mevcut sayılardan hesaplandı — orta oyun 6022/441367 = **%1.36**, oyun
        sonu 29/88208 = **%0.03** -> no-op eşiğinin (%1-2) ALTINDA. Yani işaret-tutarlı
        alt-küme ÇOK NADİR, yaygın alt-kümenin işareti YANLIŞ => terim ölü.
        Kategori kuralı korundu: sabit/varyant düzeltip retry EDİLMEDİ.
      - **KORUNDU (latent altyapı, terime bağlı DEĞİL; check-extension'da see()
        genelleştirmesinin korunması emsali):** **pawn cache artık geçer piyon KÜMESİNİ
        taşıyor** — `PawnBucket` 16 -> 32 bayt (key/data/passed_w/passed_b), lockless-XOR
        `key = pawn_key ^ data ^ pw ^ pb` (doğal genişleme, 2^16 x 32 = 2 MB);
        `pawn_structure_full(b, mg, eg, passed_w, passed_b)` (eski 3-param imza ince
        sarmalayıcı). **Şah mesafesi + rook-behind-passer bunu aynen kullanacak.**
        EXACT (düğüm birebir) ve maliyeti ölçülmüş ~0. Testler: ProbeStoreRoundtrip +
        ClearEmpties küme round-trip'ine genişletildi, `CachedPassedSetsMatchRaw` eklendi
        (cache'ten dönen küme ham hesapla birebir). 158 -> 159 test.
      - **ÖLÇÜM MİMARİ KARARI VERDİ (tahmin değil):** küme cache'te taşınmasaydı her
        eval'de yeniden üretilecekti; ağırlık=0 ile (ağaç sabit, saf maliyet izole)
        ölçüldü: **yeniden üretim %3.21 (startpos) / %5.49 (oyun sonu) nps** -> döviz
        kuruyla `log2(0.965)x195` ≈ **−10..−14 Elo**, beklenen +5-10'luk terimi garantili
        boğardı. Cache'e konunca: **−%0.73, gürültü içinde** (uzun koşu: startpos d16,
        8.67M düğüm/~5 sn, 4'er koşu; bazı koşularda yeni build daha hızlı).
        **METODOLOJİ: kısa benchmark −%1.96 diyordu, UZUN koşu −%0.73 dedi** — kısa
        koşularda %13 yayılma var, nps farkı okunamıyor. nps iddiası = uzun koşu.
- [x] **Geçer piyon şah eskortu (passer king escort) — TAMAM, SPRT H1 TAM KABUL. `8b60653`,
      YENİ BASELINE, 163 test.** Blok E3'ün İLK geçen terimi (protected passer + blockade
      ard arda H0'dan sonra) ve blockade'den kalan pawn-cache passer-kümesi altyapısını
      DOĞRULADI — başarısız denemeden kalan yatırım bir sonraki terimi geçirdi.
      **SPRT base `47fada6` vs new `8b60653`: 10483 oyun, W-D-L 3012-4649-2822, Elo +6.3 ±
      5.0, LOS %99.4, LLR 2.96 (üst sınır 2.94'ü GEÇTİ) TAM KABUL.** Not: +6.3 > elo1(5)
      olduğundan LLR tavana tırmandı (outpost +4.7'de takılıp 10k'da kalmıştı — sub-5
      bundle stratejisinin gerekçesi; bu terim sınırın az üstünde çıkıp tek başına
      certify etti, bundle'a gerek kalmadı).
      - **Mekanik:** EG-only (mg daima 0). Her geçer piyonun DURAK karesi (stop = önündeki
        kare) için `eg += g_eval.passer_king_escort_eg(8) * max(0, d_rakip − d_kendi)`;
        d = şahtan durak karesine Chebyshev (şah-hamle) mesafesi, kKingDistCap=5 ile
        kırpılı. Sıra≥3 kapısı (yalnız ilerlemiş passer). Pawn cache'in verdiği passed_w/
        passed_b kümelerini kullanır (`passer_king_escort_with(b, passed_w, passed_b, ...)`)
        -> SIFIR ek altyapı (blockade'den kalan `pawn_structure_full`).
      - **TASARIM HİKÂYESİ — enstrümantasyon ilk tasarımı SPRT'den ÖNCE öldürdü (S3 kapısı
        çalıştı):** İlk form SİMETRİK farktı (`w × (d_rakip − d_kendi)`, max'sız). "d_e ≈
        d_o ≈ 3, taban ~0" varsayımı GEOMETRİ yüzünden YANLIŞTI — durak karesi tanımı
        gereği rakip sahasında, iki mesafe simetrik değil. Ölçüm: orta oyunda kendi şah
        25169 vakanın yalnız **5'inde** (%0.02) kendi piyonuna daha yakın; `d_e − d_o`
        sıra ile güçlü negatif korele (≈4−2r) -> terim kısmen **`passed[r]`'nin negatif
        işaretli yeniden-ifadesi** oldu, `sum/çağrı = −5.006 cp` (tempo −18.6 mekanizması).
        **Enstrümantasyon ALT-KÜME KIRILIMI (kendi-yakın vs rakip-yakın) bunu yakaladı** —
        ateşleme oranı + büyüklük tek başına göstermezdi (blockade dersi: kırılım şart).
      - **DÜZELTME — tek yönlü form (`max(0, …)`):** kullanıcı kararı (AskUserQuestion).
        Tabanı sıfıra sabitler: eskort BAŞARILDIĞINDA (kendi şah daha yakın) bonus, aksi
        halde 0 -> `passed[]` ile kavga edemez. Ölçüm: `sum/çağrı` EG −5.006 -> **+0.632
        cp**; orta oyun ateşleme %6.69 -> %0.02 (tam no-op, doğru — orta oyunda şah
        eskortu kavramı yok); oyun sonu %72.7 -> %13.35. Ağırlık ölçülerek 4 -> 8 (max(0,)
        negatif yarıyı kestiğinden w=4 büyüklük bandın altında kalıyordu; w=8'de EG
        abs/çağrı 1.476 cp).
      - **ÜÇ SORU DA GEÇTİ** (bu blokta üçü de ayrı ayrı ısırmıştı): (1) adıyla sayılmıyor;
        (2) sonucuyla kısmi örtüşme var ama işareti DEĞİŞKEN (kanat passer'ında ters
        döner) — protected passer'daki *inşa gereği* zorunlu bağ gibi değil; (3) terim tek
        alt-kümede (kendi şah yakın) ateşler, orada işaret tartışmasız pozitif —
        blockade'in işaret-tutarsızlığı yapısal olarak yok.
      - **Kapılar:** 159 -> 163 test (+4: RewardsCloseOwnKing, NeverPenalizes [tek-yön
        çekirdek], IgnoresEarlyRanks, Symmetry [anti-vacuity 2. tahta]); perft birebir
        (119060324); **ağırlık=0 düğüm-eşitliği vs `47fada6` BİREBİR** (startpos d13
        1121128, cp 27, PV özdeş -> terim yalnız ağırlığı üzerinden etki eder, yan-etki
        yok). `passer_king_escort_eg` EvalParams'a frozen sınır ÖNÜNE eklendi (tunable),
        `eval_frozen_start` 824 -> 825, FrozenBoundary son-tunable "passer_king_escort_eg".
      NNUE cherry-pick ADAYI.
- [x] **Rook-behind-passer (kale kendi geçer piyonunun arkasında) — TAMAM, SPRT H1 TAM
      KABUL. `167ade2`, YENİ BASELINE, 167 test.** SPRT base `8b60653` vs new `167ade2`:
      7741 oyun, W-D-L 2172-3564-2005, Elo +7.5 ± 5.7, LOS %99.5, LLR 2.97 (2.94'ü GEÇTİ)
      TAM KABUL — E3'ün İKİNCİ geçen terimi. +7.5 > elo1(5) -> tek başına certify (escort
      gibi, bundle'a gerek yok).
      - **Mekanik:** EG-only (mg daima 0; escort/king_safety aynası — Tarrasch kuralı bir
        oyun sonu kavramı). Her kendi geçer piyonu için (cache passer kümesi), aynı sütunda +
        ARKADA (beyaz: daha düşük sıra; siyah: daha yüksek) + `between_bb(r,s) & occ == 0`
        (temiz hat) dost kale varsa `eg += 20` (passer başına bir kez). Göreli sıra≥3 kapısı
        (escort ile tutarlı). Yalnız ROOK (vezir değil). `rook_behind_passer_with(b, passed_w,
        passed_b, ...)` pawn-cache passer kümesini kullanır -> SIFIR ek altyapı (escort deseni).
      - **KAPSAM = yalnız KENDİ kale (kullanıcı kararı):** rakip-kale alt-terimi (Tarrasch'ın
        diğer yarısı) bilinçle DIŞARIDA — rakibin `rook_on_file` yarı-açık bonusuyla kısmen
        örtüşürdü (soru-2 riski). Escort'un tek-yönlü disiplininin aynısı.
      - **Üç soru da geçti:** (1) `rook_on_file` kendi passer sütununda dost piyon olduğundan
        0 -> ortogonal; (2) aynı yapı + kale var/yok iki pozisyonu mevcut eval ÖZDEŞ puanlar
        (kale PST ~düz, rook_on_file kapalı, passed_eg piyon-saf) -> yeni bilgi; (3) tek
        predicate, daima + -> blockade'in işaret-tutarsızlığı yapısal olarak yok.
      - **Kapılar:** perft birebir (119060324 / 193690690); **ağırlık=0 düğüm-eşitliği**
        (escort+rook 0 -> startpos d13 1121128/cp27/PV özdeş = `47fada6` -> sıfır yan-etki);
        enstrümantasyon (SPRT öncesi kapı) EG %8.7-42.8 ateşliyor, MG %0.01-0.03 (~0, frozen
        marjlar güvende), per-fire ~20cp (passed_eg/rook_open ile kıyaslanabilir, dominant
        değil), işaret-tutarlı. 4 test (163->167: Bonus / BlockedOrInFront / IgnoresEnemyRook /
        Symmetry [anti-vacuity iki tahta]). `rook_behind_passer_eg` frozen sınır ÖNÜNE
        (`eval_frozen_start` 825->826, FrozenBoundary son-tunable "rook_behind_passer_eg").
        Ağırlık 20 ilk elle-seçim (enstrümantasyonla gerekçeli), E7 tuning adayı.
      - **YAN BULGU (ayrı commit `19b728c`, test-only):** `PawnTable.CachedEqualsRaw`'daki
        `8/pp4pp/8/8/8/8/1P4P1/8` FEN'i ŞAHSIZDI -> E1 birleşik attack-pass'inden beri
        evaluate() daima `king_square()` çağırdığından Debug'da `std::array` OOB (Release
        sessizce çöp okur -> SPRT/perft hiç yakalamamış; baseline `8b60653`'te de vardı).
        FEN'e Ke1/ke8 eklendi (faz=0 + raw==cached değişmez). DERS: Debug ctest'i düzenli
        koş — Release-only doğrulama şahsız/yasadışı FEN sınıfı bug'ları gizler.
      NNUE cherry-pick ADAYI. Ertelenen: rank-ölçekli bonus (şu an flat), doubled-rook ödülü.
- [ ] **Passed pawn rafineleri** (ayrı commit'ler): blockade (RAFA), şah eskortu (KABUL),
      rook-behind-passer (KABUL, yukarı bak), connected/backward pawn, unstoppable/free
      passer. Beklenti +10-25 dağınık. **TUZAK (pawn-hash-cache): blockade / şah-mesafesi
      / rook-behind-passer piyon-DIŞI duruma (şah yeri, kale, blocker taşı) bağlıdır ->
      pawn-hash-cache'li `pawn_structure` İÇİNE GİREMEZ** (aynı piyon yapısı farklı taş
      dizilimiyle bayat değer döner; g_pawn_cache_enabled aynı emsal). Bu terimler cache
      DIŞINDA (geçer-piyon kare seti pawn_structure'dan/cache'ten alınıp) hesaplanmalı;
      yalnız saf-piyon terimleri (connected/protected/phalanx/backward) cache'e girebilir.
- [~] **Backward pawns — DENENDİ, RAFA KALDIRILDI (SPRT H0 TAM RED). Commit `93321c9` ->
      kod `7733d03`'ten SÖKÜLDÜ (bir sonraki commit).** SPRT base `167ade2` vs new `93321c9`
      (bad_bishop + backward BİRLİKTE): 3760 oyun, W-D-L 1011-1650-1099, **Elo −8.1 ± 8.3, LOS
      %2.8, LLR −2.95 TAM RED.** bad_bishop tek başına +4 olduğundan **backward'un marjinali ≈
      −12**. Tapered −8/−12, komşu VAR ama hepsi ileride + durak karesi rakip piyonca kontrol.
      **YENİ DERS (kritik): TÜM SPRT-ÖNCESİ KAPILARI GEÇTİ ama net-negatif.** Üç soru geçti
      (ortogonal), enstrümantasyon sağlıklı (netnz %14.6, NET |katkı|/çağrı mg 1.18 eg 1.77,
      sum/çağrı ~0 tarafsız, modest). Kapılar ORTOGONALLİĞİ + KALİBRASYONU doğrular ama
      **heuristiğin İŞARETİNİN pratikte yardımcı olduğunu DEĞİL** — geri piyon güvenilir kötü
      değil (taş kompanzasyonu / bağlam), predicate iyi-ve-kötü geri piyonları AYIRT ETMEDEN
      cezaladı (blockade işaret-tutarsızlığının daha sinsi hali: enstrümantasyon "tek predicate,
      daima −" der ama predicate'in ateşlediği pozisyonların DEĞERİ değişken). Kategori kuralı:
      sabit düzeltip retry YOK. `pawn_attack_span` engine ns'te KALDI (connected kullanıyor).
- [x] **Connected / phalanx pawns — KABUL (kullanıcı kararı, erken durdurma). YENİ BASELINE
      `2122456`, 177 test.** SPRT base `75a5f17` (bad_bishop) vs new `2122456` (bad_bishop + connected,
      connected'ı İZOLE eder): **4519 oyun, W-D-L 1287-2063-1169, Elo +9.1 ± 7.5, LOS %99.1, LLR 2.26
      (2.94'e YÜKSELİYORDU, kullanıcı erken durdurdu).** Sub-5 koşullulardan (bad-bishop/outpost, LLR
      ~1.2-1.7'de takılan) NİTELİKSEL FARKLI: +9.1 > elo1(5) -> LLR istikrarlı tırmanıyordu, tek başına
      certify eden gerçek pozitif. Erken-kabul king-safety `7eea85f` (LLR 1.46) emsalinden GÜÇLÜ; "H1
      TAM KABUL" DEĞİL (LLR 2.94'e ulaşmadı) ama sub-5 koşullulardan bir sınıf yukarı.
      - **Mekanik:** SIRA-ÖLÇEKLİ YALNIZ İLERLEMİŞ (protected-passer trap-2'sini keser): connected =
        phalanx (yatay komşu) VEYA supported (piyonca savunulan); bonus = weight × max(0, göreli_sıra−2),
        2-3. sıra 0. Tapered EG-ağır 2/4. Saf-piyon -> `pawn_structure_full` (cache); `connected_pawns()`
        izole helper. İki koruma: (1) rr>=3 gate -> izole'nin baskın olduğu düşük sıralarda susar;
        (2) "phalanx∨supported" = "sadece izole-değil"den DAR.
      - **ENSTRÜMANTASYON ön-kayıtlı passed-örtüşme RİSKİ REDDEDİLDİ** (passed-share %0.1/%6.2); netnz
        %33.4, NET |katkı|/çağrı mg 0.73 eg 1.47 (escort/outpost bandı), sum/çağrı ~0, oyun sonu eg 4.48
        -> 2/4 iyi kalibre. Kapılar: perft birebir, ağırlık=0 düğüm-eşitliği (bad_bishop+connected 0 ->
        d13 1121128/cp27 = `167ade2`), 5 test (IgnoresUnsupportedAdvanced = ortogonallik kanıtı).
      - **NOT (baseline zinciri):** connected, base `75a5f17` (bad_bishop içeren) üzerine certify etti
        -> "bad_bishop + connected" birlikte doğrulandı; bad_bishop koşullu ama connected'ın altında
        GRANDFATHERED (outpost'un escort/rook-behind altında grandfathered olması gibi). bad_bishop'un
        `167ade2`'ye karşı bağımsız certify borcu opsiyonel 9c4f6d1-bundle'da kalır. NNUE cherry-pick ADAYI.

*Blok E4 — Taş-yerleşim terimleri:*
- [x] **Knight/bishop outpost — KOŞULLU KABUL (bireysel SPRT sub-5), sonra BUNDLE-CERTIFIED
      (2026-07-18): `9c4f6d1` vs `2122456` bundle +18.8 LLR 2.94 H1 outpost'u da certify etti ->
      grandfathered borç KAPANDI, artık kesin kabul. Commit `47fada6`, 157 test.**
      **SPRT final (base `9c4f6d1` vs new `47fada6`, 1-thread, 5+0.05, elo0=0/elo1=5):
      10.000 oyun TAVAN, W-D-L 2749-4638-2613, Elo +4.7 ± 5.0, LOS %96.8, LLR 1.72
      (üst sınır 2.94'ün %58'i, YÜKSELİYORDU), beraberlik %46.4, renk dağılımı 5000/5000.**
      Kabul kullanıcı kararı — istatistiksel kesinlik DEĞİL, güçlü eğilim (**king safety
      `7eea85f` emsali: o da LLR 1.46 / LOS %99.9 ile sınıra ulaşmadan kullanıcı kararıyla
      kabul edilmişti; buradaki LLR 1.72 ondan yüksek, LOS düşük**).
      - **KUSUR TEST TASARIMINDA, TERİMDE DEĞİL:** `elo0=0/elo1=5` "bu ≥5 mi?" diye sorar;
        gerçek etki ~+4.7 ise dürüst cevap "hayır" ama terim yine de pozitif. Blok 1/3
        Ders 3 bunu zaten söylüyordu (sub-5 için daha sıkı sınır ya da sabit-N gerekir),
        sınırlar yine de 0/5 bırakıldı. **Blok bitince BUNDLE SPRT ile certify edilecek
        (`9c4f6d1` vs blok-sonu HEAD) — kullanıcı kararı, aşağıya bak.**
      - **SABIR DERSİ (LMP emsali doğrulandı):** 1319 oyunda tablo −0.3 ± 13.9 / LOS %48.5
        / LLR −0.276 idi ve "nötr çıkacak" görünüyordu. Beklenti bandı (+3-12) hata payının
        (±13.9) TAMAMEN içindeydi -> o koşu hipotezi test bile etmemişti. 10k'da +4.7 ± 5.0
        / LOS %96.8. **Erken kareye göre karar VERME**; kabul edilmiş emsaller: delta
        pruning +7.8 -> 7192 oyun, countermove +10.4 -> 4602, LMP 247 oyunda düz görünüp
        +34.5 bitti.
      - **Mekanik:** üç kapı — göreli sıra 4-6, dost piyon desteği (`by_pawn[c]`, E1'in
        birleşik attack-pass'inde zaten var), kovulabilirlik. `attack_eval_impl`'in mevcut
        at/fil döngülerine ayrı `add_outpost` lambda'sı (`add`'e DOKUNULMADI: `add`'in işi
        "atak setini aggregate'e ekle", outpost yerleşim terimi -> `add`'e kare parametresi
        kale/veziri ölü argümanla kirletirdi). `enemy_pawns` renk döngüsüne hoist.
      - **Maske YENİ TABLO İSTEMEDİ:** `PassedMask[c][s] & AdjacentFileMask[file_of(s)]` =
        komşu sütunlar × s'nin önü = Stockfish'in `pawn_attacks_span`'i. Kaba kuvvetle
        bağımsız ground-truth'a karşı 128 (renk,kare) çiftinde **0 uyumsuzluk**.
        NOT: "kovamaz" = KENDİ SÜTUNUNDA İLERLEYEREK kovamaz (piyon vuruş yapıp sütun
        değiştirip sonradan kovabilir; SF de vuruşları saymaz — bilinçli sadeleştirme).
        İSİM TUZAĞI: `eval.cpp`'de zaten `pawn_attack_span` var ve FARKLI şey demek
        (tek adımlık vuruşlar) -> maske inline yazıldı.
      - **YENİ TEKNİK — ağırlık=0 hilesi (sezgisel eval terimi için yan-etki + nps kapısı):**
        outpost ağırlıkları EvalFile ile 0 -> eval baseline ile SAYISAL ÖZDEŞ -> ağaç da
        özdeş olmalı. Çıktı: startpos d13 = 962829 (cp 35, e2e4), Kiwipete d12 = 714878
        (cp −62, e2a6), PV dahil `9c4f6d1` ile BİREBİR => **terim yalnız ağırlıkları
        üzerinden etki ediyor** (sezgisel terimde normalde düğüm-eşitliği kapısı YOKTUR).
        Aynı hile nps'i de kurtardı: normal ölçüm −%4.8 diyordu ama KİRLİ (ağaç değişmiş,
        düğüm +%16); sabit ağaçta gerçek maliyet **~%1.5**. Genelleme: sezgisel eval
        teriminde nps'i ağırlık=0 ile ölç. Perft birebir.
      - **Enstrümantasyon (SPRT öncesi kapı) SAĞLIKLI:** 5 gerçek orta oyun × d12 ->
        eval'lerin %9.40'ında ateşliyor (no-op değil), `abs/call` 2.02 cp, `sum/call`
        0.68 cp (ölçek kaydırmıyor), fil/at ateşleme oranı 0.36 -> at-ağırlıklı (paket
        fil-dominant DEĞİL -> H0'da ayrışma sorunu olmazdı).
      - **ÇÜRÜTÜLEN TAHMİN (ölçüldü):** planda "kovulamaz koşulu EG'de bedavaya doğru olur
        -> terim EG'de şişer, EG ağırlığı kısılmalı" YANLIŞ — at başına ateşleme fazlar
        arası DÜZ (%2.49 orta oyun / %2.70 oyun sonu). Sebep: rakip piyon azalınca
        kovulabilirlik gevşiyor (rank_ok %27->%52) ama AYNI anda dost piyon desteği
        zorlaşıyor (support_ok %47->%20) -> götürüyor. **Destek koşulu terimi kendi kendine
        sınırlıyor.** MG>EG artık satranç gerekçesine dayanıyor (outpost orta oyun kavramı),
        ölçek korkusuna değil.
      - At 25/15, fil 12/8 (at>fil 2:1: at kısa menzilli, kalıcı-korunan kare uzağa baskının
        tek yolu). 4 skaler frozen sınır ÖNÜNE, `eval_frozen_start` 820->824, FrozenBoundary
        -> "outpost_bishop_eg". At+fil TEK COMMIT: threats emsali + kodda zaten
        `mobility_mg[PIECE_TYPE_NB]`/`king_attack_weight[PIECE_TYPE_NB]` emsali (tek terim,
        taş-türü başına sabit); Blok 1/3 yasağı BAĞIMSIZ mekanizmaları hedefler, burada tek
        predicate + iki sabit var. 5 test (152->157; OutpostKnightBonus'un 2. tahtası
        ANTI-VACUITY: siyah piyon d5'in ARKASINDA -> bonus bozulmamalı; öne-kısıtını unutan
        naif implementasyonu yakalayan TEK test). NNUE cherry-pick ADAYI (bundle H1 sonrası).
- [x] **Bad bishop (kendi renginde piyon sayısı) — KOŞULLU KABUL (bireysel SPRT sub-5 +4),
      sonra BUNDLE-CERTIFIED (2026-07-18): `9c4f6d1` vs `2122456` bundle +18.8 LLR 2.94 H1 bad-bishop'u
      da certify etti -> grandfathered borç KAPANDI, artık kesin kabul. Commit `75a5f17`, 172 test.**
      **SPRT base `167ade2` vs new `75a5f17` (1-thread, 5+0.05, elo0=0/elo1=5): 10779 oyun,
      W-D-L 3022-4863-2894, Elo +4 ± 4.9, LOS %94.7, LLR 1.22 (2.94'e sürünüyordu, kullanıcı
      durdurdu).** OUTPOST DESENİNİN BİREBİR AYNISI (outpost +4.7 ± 5.0, LOS %96.8, LLR 1.72
      @10k) -> sub-5 etki 0/5 sınırında tek başına certify EDİLEMEZ (Blok 1/3 Ders 3).
      Koşullu tutuldu (LOS %94.7 > %50 + nokta tahmini +); blok sonu BUNDLE SPRT'ye
      (base `9c4f6d1`) girer. "H1 KABUL" DİYE ETİKETLENMEDİ (kayıt dürüstlüğü, king safety
      `7eea85f` / outpost emsali).
      - **Mekanik:** tapered (EG-ağır), E1'in birleşik attack-pass'ine fold (`attack_eval_impl`
        `bb_mg/bb_eg`; fil döngüsünde `add_bad_bishop` lambda, own/sign/occ paylaşır). Fil
        başına ceza: `BadBishop{Mg,Eg} * (#fil-renginde dost piyon) + BadBishopBlocked{Mg,Eg}
        * (#bunlardan blokeli)`, `+= -sign*pen` (king_safety danger deseni). Yeni
        `SquareColorMask` constexpr (koyu/açık). 4 skaler frozen sınır ÖNÜNE (tunable;
        `eval_frozen_start` 826->830, FrozenBoundary son-tunable "bad_bishop_blocked_eg").
      - **Kullanıcı kararları:** (a) tapered EG-ağır (outpost/threats gibi; pozisyona-bağlı +
        renk-simetrik -> tempo/tune-all ölçek-kayması failure mode'u yapısal yok); (b) blocked
        ağır tartılır (mobility'nin -tek-ply snapshot- hafife aldığı KALICI yapısal zaafiyet).
      - **ÜÇ SORU DA GEÇTİ:** (1) adıyla sayılmıyor; (2) already-priced elle-kontrol: aynı
        yapıda iyi-renk vs kötü-renk fil pozisyonlarını mevcut eval yalnız ~2 cp ayırıyor
        (işareti bile ters, PST) -> sinyal YENİ, mobility yeniden-ifadesi değil; (3) işaret
        daima negatif, tek predicate -> blockade işaret-tutarsızlığı yok.
      - **AĞIRLIK ENSTRÜMANTASYONLA KISILDI (SPRT öncesi kapı):** ilk 2/3/3/5 -> NET |katkı|/
        çağrı orta oyun mg 7.0 / eg 11.0 cp (escort 1.3 / outpost 2.0'ın 3-5 katı) + fil'lerin
        %99.99'unda ateşliyor (ort 3.77 aynı-renk piyon = normal fil de cezalanıyor). NET
        işaretli/çağrı ≈ 0 (renk-simetrik -> ölçek kaymıyor). Modest banda çekildi: **1/2/2/3**
        (NET ~mg 3.9 / eg 2.7; blocked hâlâ ağır 2>1, 3>2; EG hâlâ ağır). Kısılmış ağırlık
        aramayı baseline'a çok yakın tuttu (startpos d13 default 1202491 vs baseline 1121128,
        +%7, bestmove b1c3 AYNI; 2/3/3/5 -%30 + farklı bestmove veriyordu). E7 joint tuning adayı.
      - Kapılar: 167->172 test (BadBishopPenalty / BlockedExtra / IgnoresOppositeColor /
        IgnoresEnemyPawns / Symmetry; Debug+Release geçti). Perft birebir. Ağırlık=0 düğüm-
        eşitliği: EvalFile ile dört ağırlık 0 -> startpos d13 1121128/cp27/PV = `167ade2`
        birebir (sıfır yan-etki). NNUE cherry-pick ADAYI (bundle H1 sonrası).
- [x] **Rook on 7th (gated) — TAMAM, SPRT KABUL (kullanıcı kararı, erken durdurma).
      `8ada3d8`, YENİ BASELINE, 181 test.** SPRT base `2122456` vs new `8ada3d8`: 5988
      oyun, W-D-L 1739-2635-1614, Elo +7 ± 6.6, LOS %98.2, LLR 2.0 (2.94'e yükseliyordu,
      kullanıcı erken durdurdu). **+7 > elo1(5) -> tek başına certify** (escort +6.3 /
      rook-behind +7.5 sınıfı; connected +9.1/LLR 2.26 erken-durdurma emsali) -> bundle'a
      gerek yok. "H1 TAM KABUL" (LLR 2.94) DEĞİL (dürüst etiket) ama gerçek pozitif,
      sub-5 koşullulardan (outpost/bad-bishop, LLR 1.2-1.7'de takılan) BİR SINIF YUKARI.
      - **Mekanik:** izole helper `rook_on_seventh` (bishop_pair/rook_on_file deseni;
        kendi kale döngüsü, attack_eval_impl'e girmez). Kale rakip 7. sırasında VE
        (rakip şah 8.'de VEYA rakip piyon 7.'de) -> kale başına bonus (7.'de çift kale
        doğal 2×; "pigs on 7th" ek mantıksız). BEYAZ−SİYAH, tapered **mg 15 / eg 20**.
        `eval_accumulate`'te `rook_on_file`'dan hemen sonra çağrılır. Gate zorunlu:
        `RawPST[ROOK]` 7. sıraya düz +10 verir -> naif form yeniden-ifade olurdu.
      - **ÜÇ SORU DA GEÇTİ:** (1) ortogonal — PST düz +10 verir ama gate'i AYIRT ETMEZ;
        (2) already-priced elle-eval (SPRT öncesi) — king-on-8th dalı: baseline eval iki
        şah-yerleşimini (Rd7, siyah şah e8 vs g6) yalnız **−9 cp** ayırıyor (ters yönde)
        -> dal yeniden-ifade DEĞİL, iki gate dalı da tutuldu; (3) tek predicate, daima
        +sign (blockade işaret-tutarsızlığı yapısal yok).
      - **ENSTRÜMANTASYON (geçici, söküldü):** midgame ateşleme **%0.36 (near-no-op)** ->
        frozen midgame marjları YAPISAL güvende (tempo/tune-all tuzağı yok — term midgame
        static_eval'i kaydırmıyor); endgame %53; gate-split sağlıklı (pawn-only %34,
        both %57, king-only %8.5 -> iki dal da sinyal taşır). KALİBRASYON: ilk 20/25
        endgame NET |eg|/call=13.3cp (bad_bishop pre-trim bandı) -> **15/20**'ye kısıldı;
        eg proven-sibling `rook_behind_passer_eg=20`'ye çıpalandı (+7.5 geçmişti, ~aynı
        per-fire 20cp / endgame firing).
      - **Kapılar:** perft birebir (119060324 / 193690690); **ağırlık=0 düğüm-eşitliği**
        (EvalFile ile term=0 -> baseline `2122456` ile BİREBİR: startpos d13 876562/cp31,
        Kiwipete d12 528002/cp-44, PV dahil -> sıfır yan-etki); 4 test (177->181:
        RookOnSeventhBonus [king-on-8th dalı], PawnGate [pawns-on-7th dalı], GateClosed
        [ortogonallik], Symmetry [anti-vacuity]). `rook_on_seventh_eg` frozen sınır önüne
        (`eval_frozen_start` 832->834, FrozenBoundary son-tunable "rook_on_seventh_eg").
      - Ağırlık 15/20 enstrümantasyonla gerekçeli, E7 tuning adayı. NNUE cherry-pick ADAYI.
      Ertelenen (ayrı commit adayı): connected-rooks (7.'de çift kale zaten 2× alıyor,
      EK bonus ayrı terim), rook-trapped-by-king, rank-ölçekli bonus, RAKİP-kale 7.-sıra.
- [~] **E4-KALANI KAPANDI (2026-07-23, ÖLÇÜMLE — SPRT HARCANMADAN; kullanıcı kararı).**
      Roadmap'in bıraktığı üç aday ÖN-TARAMA ile elendi: tek atılabilir enstrümantasyon
      build'i (commit edilmedi), ~918K eval (5 orta oyun d12 + iki ayrı 6-pozisyonluk
      oyun sonu seti d14). Hiçbiri SPRT'ye girmedi -> blockade "minör-only" emsalinin
      (ölçüm varyantı ek SPRT'siz eledi) ikinci uygulaması.
      - **ROADMAP BELİRSİZLİĞİ ÇÖZÜLDÜ: "RAKİP-kale 7.-sıra" SAF NO-OP olurdu** —
        `rook_on_seventh` zaten `for (Color c : {WHITE, BLACK})` + sign ile TAM SİMETRİK,
        rakip kalenin kendi 7.'sinde olması hâlihazırda cezalanıyor. Kastedilen (parantez:
        "rook_on_file yarı-açık ile örtüşebilir") **rook-behind-passer'ın RAKİP yarısı**:
        rakip kale BİZİM passer'ımızın arkasında (Tarrasch'ın `167ade2`'de dışarıda
        bırakılan yarısı). Ölçülen aday buydu.
      - **Aday: pigs on 7th (7.'de çift kale EK bonusu) — ÖLÜ.** ~918K eval'de **0 (SIFIR)
        ateşleme**; aynı koşularda tek-kale gate'i %2.43 (orta oyun) / %15.6 / %6.2
        (oyun sonu) ateşliyor. No-op eşiğinin (~%1-2) çok altında -> terim ölü, SPRT yok.
      - **Aday: rakip kale bizim passer'ımızın arkasında — ÖLÇÜM ELEDİ (H0 DEĞİL, hiç
        denenmedi).** Orta oyun %0.003 (no-op), oyun sonu %3.24 / %6.26. Üç bulgu:
        (a) **ÖN-KAYITLI soru-2 riski MAKSİMUMDA geldi: ateşlemelerin %100'ünde** o sütun
        rakip için yarı-açık -> `rook_semi` (12/8) zaten ödüyor (yapısal: passer'ın
        sütununda rakip piyon bulunması nadir); (b) terimin tek yeni bilgisi "arkada mı
        önde mi" AYRIMI, ama dağılım KÖK YERLEŞİMİYLE belirleniyor (set-1 %84.7 önde,
        set-2 %95.4 arkada) -> pozisyon İÇİNDE aramanın çözdüğü bir ayrım değil, ~sabit
        kaydırma; (c) **büyüklük 10× küçük**: kendi lehine kurulmuş sette NET |katkı|/çağrı
        0.75 cp (w=12), kardeşi (KABUL EDİLMİŞ own-rook-behind, +7.5 Elo) kendi lehine
        sette %38.8 ateşleme / **7.77 cp/çağrı**. Kabul edilen terim bandına (escort 1.48,
        outpost 2.02) çıkmak ağırlık ~25-40 ister -> kısmen çift-sayılan sinyalde
        kardeşinin 20'sinin üstü, savunulamaz. Beklenti +1..+3 -> `elo0=0/elo1=5`'in
        çözemediği bant (Blok 1/3 Ders 3; outpost/bad-bishop deseni) + E4'te bundle
        ortağı kalmadı.
      - **YENİ METODOLOJİ KAZANIMI — KIYAS ÖLÇÜMÜ:** aday terimin ateşleme/büyüklüğünü
        AYNI POZİSYONLARDA kabul edilmiş bir kardeş terimle yan yana ölç. Mutlak sayılar
        (%3-6 ateşleme, 0.4-0.8 cp) tek başına "sağlıklı" görünüyordu — backward dersi
        (tüm kapıları geçip −12) bu okumanın yetmediğini zaten göstermişti. Kardeş oranı
        (10×) ise beklenen etkiyi SPRT'den önce bantladı. Ayrıca: **ateşleme dağılımı
        kök yerleşimine duyarlıysa (iki zıt sette %85 vs %95), terim aramanın çözdüğü
        bir ayrım değil ~sabit bir kaydırmadır** — tek bir FEN setiyle ölçmek yanıltır.
      - **Kalanlar:** rank-ölçekli rook_behind_passer -> **E7** (kabul edilmiş terimin
        yeniden kalibrasyonu + passed[r] ile aynı eksene ikinci gradyan = protected-passer
        tuzağının şekli); rook-trapped-by-king -> ATLANDI (mobility zaten düşük kare
        sayısını cezalandırıyor, soru-2 riski); bishop-pair zıt-kare -> ATLANDI (aşağı bak).
- [ ] **Bishop pair zıt-kare rafinesi** (mevcut basit ≥2 sayımı). Her biri +3-12.
      NOT: near-no-op adayı (2 fil ~%99.9 zıt-renk; yalnız nadir çift-promosyon farkı).
      KARAR (2026-07-23): ATLANIYOR — E4 kapandı, sıra E5'te.

**SUB-5 TERİM STRATEJİSİ — BUNDLE SPRT (kullanıcı kararı, 2026-07-16).** Faz 3'ün kalan
eval terimlerinin ÇOĞU +3-15 bandında bekleniyor; outpost (+4.7 ± 5.0, LOS %96.8, LLR 1.72
@10k tavan) sub-5 bir etkinin `elo0=0/elo1=5` ile TEK BAŞINA certify EDİLEMEYECEĞİNİ
kanıtladı (improving-revize `2ce0352` +1.2 ± 5.3 aynı desen). **Yeni akış:**
1. Her terim ayrı commit + ayrı SPRT + enstrümantasyon + yan-etki kapısı (mevcut disiplin
   AYNEN korunur).
2. SPRT kararsız ama POZİTİF eğilimliyse (LOS belirgin >%50, nokta tahmini +) terim
   **KOŞULLU** tutulur ve **"H1 KABUL" DİYE ETİKETLENMEZ** (kaydın dürüstlüğü bu reponun
   en değerli varlığı — EN KRİTİK DERS'in gücü 10+ veri noktasının gerçekten ölçülmüş
   olmasından geliyor). H0/negatif eğilimli terim koşullu TUTULMAZ, anında revert edilir
   (kategori kuralı geçerli: sabit düzeltip retry ETME).
3. Blok bitince biriken TÜM terimler bloğun BAŞLANGIÇ baseline'ına karşı **TEK BUNDLE
   SPRT**'ye sokulur (E3/E4 için base `9c4f6d1`). Birkaç +3-5 terimin toplamı +12-15 ->
   rahat certify eder. Bundle H1 -> koşullu etiketler kalkar, blok sağlam baseline olur.
**Paketleme yasağını (Blok 1/3) İHLAL ETMEZ:** o yasak *tanı koymayı* korur (improving'de
üç bağımsız kaplama tek SPRT'de H0 verince hangisi zararlı ayrışmadı); burada her terim
ZATEN tek tek ölçülüyor, bundle yalnız TOPLAMI doğruluyor. **Risk (dürüstçe):** bundle H0
çıkarsa hangi terimin battığı ayrışmaz -> tek tek geri çıkmak gerekir; bu yüzden bundle'a
yalnız pozitif eğilimli terimler girer.

*Blok E5 — Endgame ölçekleme (HAFİF; kullanıcı kararı — tam KPK/KBNK bilgisi HARİÇ).
AKTİF BLOK (2026-07-23).*

**ÖN-TARAMA ÖNCE (E4 dersinin uygulaması): üç kural ölçüldü, sıralamayı ÖLÇÜM belirledi.**
Yöntem: atılabilir enstrümantasyon (commit edilmedi) + **150 oyunluk self-play datagen**.
YAPISAL BULGU: bu kurallar normal ağaçta ~%0 ateşler (ağaç kök materyal sınıfından
çıkmaz) ama KENDİ sınıfında %34-42 -> Elo "ağaçta ne sıklıkta" ile değil "gerçek oyunlar
bu sınıfa ne sıklıkta ulaşıyor" ile belirlenir; **tree-instrumentation YETMEZ, self-play
pozisyon dağılımı gerekir** (E4'te kullanılan ölçüm burada yanıltıcı olurdu).
| kural | gerçek oyun pozisyonları |
|---|---|
| zıt-renk fil (OCB) | **%5.23** (hepsi piyon farkı ≤2 = asıl drawish alt-küme) |
| piyonsuz + küçük materyal farkı | %1.48 |
| yanlış kale-piyonu | **%0.000 -> ÖLÜ** |

- [x] **Scaling factor altyapısı + zıt-renk fil (OCB) kuralı — KOD TAMAM, SPRT BEKLİYOR
      (commit `c40dfb9`, 185 test).** Altyapı tek başına no-op -> ayrı SPRT'si anlamsız;
      önkoşul zinciri olarak TEK COMMIT (cont-hist / null-gate emsali).
      - **Mekanik:** `endgame_scale(b)` çarpanı `eval_accumulate` SONUNDA yalnız `eg`'e
        uygulanır (`eg * scale / ScaleNormal(64)`); `mg` DOKUNULMAZ -> cp-kalibre
        DONDURULMUŞ arama marjları yapısal olarak güvende, üstelik ölçek yalnızca |eg|'i
        KÜÇÜLTÜR (tempo/tune-all şişirme yönünün TERSİ). Kural: piyon dışı materyal her
        iki tarafta tam bir fil (tahtada hiç at/kale/vezir yok) + filler zıt renk ->
        `OcbScale = 32/64`. Tam sayı bölmesi sıfıra doğru kırpar -> ±eg simetrisi birebir.
      - **Neden `evaluate()` değil `eval_accumulate` sonu:** tuner özelliklerini de
        oradan türetir -> model ile motor ıraksamaz. NOT: ölçek ÇARPANDIR, tuner'ın
        doğrusal modeli onu özel ele almalı (E7 borcu).
      - **ÜÇ SORU DA GEÇTİ:** (1) bishop_pair ≥2 fil ister (burada 1+1), bad_bishop filin
        KENDİ piyon renklerini cezalandırır (zıt-renk ilişkisini bilmez), materyal fillere
        eşit değer verir; (2) ölçüldü — baseline OCB'de +1 piyonu **+287 cp** okuyor,
        hiçbir terim "çevrilemezlik" boyutunu taşımıyor; (3) tek predicate, daima |eg|
        küçültür, renk-simetrik.
      - **ÖN-KAYITLI RİSK:** (a) 2+ piyon fazla OCB kazanılabilir; tek sabit 32/64 o
        alt-kümede kazancı hafife alabilir (SF ~22/64 ama piyon sayısı/passer'a göre
        DEĞİŞKEN ölçek kullanır); (b) 5+0.05 hızlı TC'de oyunların ne kadarı OCB oyun
        sonuna ULAŞIYOR belirsiz -> ulaşmıyorsa etki çözülemez bant (outpost deseni).
      - **Kapılar:** perft birebir; **ağırlık=0 düğüm-eşitliği** (EvalFile ile
        `ocb_scale=64` -> `8ada3d8` ile BİREBİR: startpos d13 876564/cp31, d16 5547746,
        Kiwipete d12 531476/cp−44, PV dahil); nps −%0.5 (uzun koşu, 5.55M düğüm ×3, taban
        yayılma %1.5 -> ihmal); 185/185 test Debug+Release (+4). Sağlama (kapı değil):
        OCB +1 piyon base +287 -> yeni +153 cp, bestmove aynı.
      - `ocb_scale` frozen sınır ÖNÜNE (`eval_frozen_start` 834->835, FrozenBoundary
        son-tunable "ocb_scale"). 32/64 ilk elle-seçim, E7 tuning adayı.
- [ ] **Piyonsuz + küçük materyal farkı** (güçlü tarafın piyonu yok + piyon-dışı fark
      ≤ fil -> KRvKB/KBvKN vb. çevrilemez): ön-taramada **%1.48**, ateşlediğinde baseline
      ort **|eg| 376 cp** okuyor (KRvKB'yi +3.7 piyon sanıyor). OCB'den sonra AYRI commit
      + AYRI SPRT (paketleme yasağı).
- [~] **Yanlış kale-piyonu** (a/h piyonu + yanlış köşe fili) — **ÖLÇÜMLE ELENDİ
      (%0.000, SPRT yok).** Kanonik pozisyonunda doğru ateşliyor (baseline orada +575 cp
      okuyor) ama gerçek oyunlar bu sınıfa hiç ulaşmıyor -> pigs-on-7th ile aynı kader.
- [ ] **Genel drawish scaling** (tek minör, düşük materyal). Beklenti +5-20, güvenli.

*Blok E6 — Küçükler:*
- [ ] **Tempo (cerrahi varyant)**: Blok 3/12 dersi — tempo yalnız yaprak/qsearch
      stand-pat DÖNÜŞÜNE eklenir; budama kapıları tempo'suz `static_eval` üzerinde
      kalır (RFP/null marjları kaymaz). `evaluate()` içi versiyon −18.6 vermişti.
- [ ] (opsiyonel) **Space**, **contempt (draw≠0)**.

*Blok E7 — Texel tuning retry (SONA, opsiyonel büyük; kullanıcı kararı):*
- [ ] E2-E6'daki tüm yeni terimler EvalParams'a eklendiğinden tuner otomatik kapsar
      (daha zengin parametre uzayı). Altyapı zaten var (Blok 4: datagen/tuner/EvalFile),
      ilk geçiş ~nötr çıkmıştı (sığ veri + ölçek bağı).
- [ ] **Daha derin/çeşitli veri** (datagen depth artır, açılış çeşitliliği) —
      "veri tavanı" teşhisini hedefler.
- [ ] **King-safety JOINT tuning** (dondurulmuş nonlinear `safety_table` + weights +
      E2'den ertelenen rafineler BİRLİKTE). E2'nin iki king-safety denemesi (açık-hat H0,
      piyon-ring ~nötr) tek tek elle enjeksiyonun frozen tabloda çalışmadığını kanıtladı
      -> pawn storm / safe check / attacker-count / ring'e-piyon / açık-hat burada, safety
      alt-sistemi bir bütün olarak tune edilerek eklenir (tek tek elle-SPRT DEĞİL).
- [ ] **Joint arama-marj tuning** (Blok 4/16 ile birleşik): eval-ölçek↔marj bağını
      çözmek için RFP/futility/razor/LMP/null marjlarını da tune et (tune-all −110'un
      kök sebebi buydu).

Bu faz iteratif: her madde tek başına doğrulanır. Faz 1-2 sağlam oturdu, artık
tek tek eval terimleri ekleyip SPRT'den geçiriyoruz.

### Şu an nerede olduğumuzu takip et

Her oturum başında bana hangi fazda, hangi adımda olduğumuzu hatırlat. Eğer bir
önceki oturumdan kalan yarım iş varsa (örneğin test yazılmamış bir fonksiyon,
geçmeyen bir perft testi) önce onu bitirmeden yeni özelliğe geçme.

**Güncel durum (2026-07-23): FAZ 1 + FAZ 2A + FAZ 2B + FAZ 2C(-devam) + FAZ 2D TAMAM.
Klasik motorun arama/movegen/SMP tarafı bitti. FAZ 3 (klasik) — Eval Güçlendirme
DEVAM: BLOK E1 (altyapı+hız) TAM TAMAM, BLOK E2 Commit 1 (threats/hanging) KABUL
(H1, baseline `9c4f6d1`), BLOK E2 king-safety rafineleri E7 JOINT TUNING'e ERTELENDİ
(iki deneme H0/nötr, kategori kanıtı aleyhte). **BLOK E3 rook-behind-passer (kale geçer
piyon arkasında) KABUL -> YENİ BASELINE `167ade2`, 167 test** (SPRT H1 TAM KABUL: +7.5 ±
5.7, LOS %99.5, LLR 2.97/7741 oyun — E3'ün İKİNCİ geçen terimi; +7.5 > elo1 -> tek başına
certify). Öncesi BLOK E3 şah eskortu KABUL -> `8b60653` (+6.3 ± 5.0, LOS %99.4). **BLOK E4
bad bishop KOŞULLU KABUL (sub-5 pozitif, certify EDİLMEDİ) -> commit `75a5f17`, 172 test;
HEAD koşullu (baseline yine `167ade2`).** SPRT base `167ade2` vs new `75a5f17`: 10779 oyun,
Elo +4 ± 4.9, LOS %94.7, LLR 1.22 (kullanıcı durdurdu) — OUTPOST DESENİ (sub-5, 0/5'te tek
certify olmaz). Öncesi BLOK E4 outpost KOŞULLU KABUL -> `47fada6`, 157 test (10k tavan:
+4.7 ± 5.0, LOS %96.8, LLR 1.72 — H1 DEĞİL). **BLOK E3 backward pawns DENENDİ/RAFA (SPRT H0 −8.1,
kod `2122456`'da söküldü). BLOK E3 connected/phalanx pawns KABUL (kullanıcı kararı, erken durdurma;
+9.1 ± 7.5, LOS %99.1, LLR 2.26 yükseliyordu) -> BASELINE `2122456`, 177 test.** **BLOK E3/E4 EVAL
BUNDLE SPRT TAM KABUL: base `9c4f6d1` (threats) vs `2122456` (HEAD): 2428 oyun, W-D-L 741-1077-610,
Elo +18.8 ± 10.3, LOS %100, LLR 2.94 TAM H1 -> threats sonrası hayatta kalan 5 terimin (outpost +
escort + rook-behind + bad-bishop + connected) KÜMÜLATİF etkisi certify; GRANDFATHERED BORÇ KAPANDI
(outpost + bad-bishop artık bundle-certified), `2122456` TAM-CERTIFIED BASELINE.** **SIRADAKİ İŞ:
BLOK E3 BİTTİ + eval bundle certified, sonra BLOK E4 rook-on-7th (gated) KABUL -> **BASELINE
`8ada3d8`, 181 test**. **E4-KALANI (2026-07-23) ÖLÇÜMLE KAPANDI, SPRT HARCANMADAN: E4 BİTTİ.**
SIRADAKİ: **BLOK E5 — endgame scaling** (ortogonal+güvenli, untapped).** NNUE bu repoda
YOK, ayrı repoda (`../ChessEngineNNUE`).**
**SON (2026-07-23): BLOK E4 KAPANDI — E4-kalanı üç adayı da ÖN-TARAMA ile elendi, HİÇBİRİ
SPRT'ye girmedi (kullanıcı kararı). Tek atılabilir enstrümantasyon build'i (commit edilmedi),
~918K eval (5 orta oyun d12 + iki zıt kurgulu 6-pozisyonluk oyun sonu seti d14); kod `8ada3d8`'e
birebir geri alındı (`git status` temiz), baseline DEĞİŞMEDİ, 181 test. (1) **Roadmap belirsizliği
çözüldü:** "RAKİP-kale 7.-sıra" saf no-op olurdu — `rook_on_seventh` zaten iki renk üzerinde
simetrik; kastedilen rook-behind-passer'ın RAKİP yarısıydı. (2) **pigs-on-7th ÖLÜ:** ~918K eval'de
SIFIR ateşleme (tek-kale gate'i aynı koşuda %2.43/%15.6/%6.2) -> no-op eşiğinin çok altı. (3)
**rakip-kale-passer-arkasında ÖLÇÜM ELEDİ:** orta oyun %0.003, oyun sonu %3.24/%6.26; ateşlemelerin
**%100'ü rook_semi ile örtüşüyor** (ön-kayıtlı soru-2 riski maksimumda); "arkada vs önde" ayrımı
KÖK YERLEŞİMİYLE belirleniyor (%84.7 önde vs %95.4 arkada, iki zıt sette) -> aramanın çözdüğü
ayrım değil ~sabit kaydırma; büyüklük kardeşinin 1/10'u (0.75 vs 7.77 cp/çağrı, her biri kendi
lehine sette) -> beklenti +1..+3, 0/5 SPRT'nin çözemediği bant + E4'te bundle ortağı yok.
**YENİ METODOLOJİ — KIYAS ÖLÇÜMÜ: adayı AYNI pozisyonlarda kabul edilmiş bir kardeş terimle yan
yana ölç** (mutlak %3-6 / 0.4-0.8 cp "sağlıklı" görünüyordu; 10× oranı beklentiyi SPRT'den önce
bantladı — backward dersinin, "kapılar işaretin yardımcı olduğunu kanıtlamaz"ın niceliksel
tamamlayıcısı). **İKİNCİ ÖLÇÜM DERSİ: ateşleme dağılımı kök yerleşimine duyarlıysa tek FEN
setiyle ölçmek yanıltır** — zıt kurgulu İKİ set kullan. Kalanlar: rank-ölçekli -> E7,
trapped-rook + bishop-pair zıt-kare -> atlandı. SIRADAKİ: **BLOK E5 endgame scaling**.**
**SON (2026-07-18): Blok E4 kale 7. sırada (gated) KABUL (kullanıcı kararı, erken durdurma) ->
YENİ BASELINE `8ada3d8`, 181 test. SPRT base `2122456` vs new `8ada3d8`: 5988 oyun, W-D-L
1739-2635-1614, Elo +7 ± 6.6, LOS %98.2, LLR 2.0 (2.94'e yükseliyordu, kullanıcı erken durdurdu).
+7 > elo1(5) -> TEK BAŞINA CERTIFY (escort +6.3 / rook-behind +7.5 sınıfı; connected +9.1/LLR 2.26
erken-durdurma emsali) -> bundle'a gerek yok. "H1 TAM KABUL" (LLR 2.94) DEĞİL (dürüst etiket) ama
gerçek pozitif, sub-5 koşullulardan (outpost/bad-bishop, LLR 1.2-1.7'de takılan) BİR SINIF YUKARI.
Mekanik: izole helper rook_on_seventh (kendi kale döngüsü), kale rakip 7.'sinde VE (rakip şah 8.'de
VEYA rakip piyon 7.'de) -> kale başına bonus (çift kale 2×), tapered mg 15/eg 20; gate zorunlu
(RawPST[ROOK] 7. sıraya düz +10 -> naif form yeniden-ifade). ÜÇ SORU DA GEÇTİ: (1) ortogonal (PST
gate'i ayırt etmez); (2) already-priced elle-eval — king-on-8th dalı baseline'da yalnız −9 cp ayrım
(ters yönde) -> re-expression DEĞİL; (3) tek predicate daima +. ENSTRÜMANTASYON (söküldü): midgame
%0.36 near-no-op (frozen marjlar YAPISAL güvende), endgame %53, gate-split sağlıklı (pawn %34/both
%57/king %8.5). KALİBRASYON: 20/25 -> 15/20 (eg rook_behind_passer=20'ye çıpalandı; ilk endgame NET
|eg|/call 13.3cp bad_bishop pre-trim bandı). Kapılar: perft birebir, ağırlık=0 düğüm-eşitliği (term=0
-> baseline `2122456` BİREBİR: d13 876562/cp31, Kiwipete d12 528002/cp-44), 4 test (177->181),
FrozenBoundary son-tunable "rook_on_seventh_eg" (eval_frozen_start 832->834). NNUE cherry-pick ADAYI.
DÜRÜST GÖZLEM: kod commit `8ada3d8` = terim; SPRT bu commit'e karşı koşuldu, kabul sonrası ayrı
roadmap-commit ile kaydedildi. Kalan E4/E5 kullanıcı kararı.**
**SON (2026-07-18): BLOK E3/E4 EVAL BUNDLE SPRT TAM KABUL (H1). base `9c4f6d1` (E2 threats baseline,
tüm eval-güçlendirme işinin başı) vs new `2122456` (HEAD): 2428 oyun, W-D-L 741-1077-610, Elo +18.8
± 10.3, LOS %100, LLR 2.94 TAM H1. Threats sonrası HAYATTA KALAN 5 terimin (outpost + escort +
rook-behind + bad-bishop + connected; backward/protected/blockade RAFA) KÜMÜLATİF etkisini certify
etti -> **GRANDFATHERED BORÇ KAPANDI**: outpost (+4.7 koşullu) + bad-bishop (+4 koşullu) hiç bağımsız
certify olmamıştı, bundle ikisini de certify etti; `2122456` artık TAM-CERTIFIED BASELINE (koşullu
caveat yok). DÜRÜST GÖZLEM: kümülatif +18.8, bireysel toplamdan (~+31.6) DÜŞÜK — normal (motor
güçlendikçe azalan getiri + kısmi terim örtüşmesi; bireysel SPRT'ler daha zayıf tabana karşı ölçtüğü
için şişer). SUB-5 BUNDLE STRATEJİSİ DOĞRULANDI: tek tek certify edilemeyen sub-5 terimler (outpost/
bad-bishop) bundle'da certify oldu; bireysel SPRT'ler önce negatif terimi (backward −8.1) bundle'a
girmeden eledi -> stratejinin iki yarısı da işledi. E3 BİTTİ; sıradaki E4 rook-on-7th/bishop-pair
(kullanıcı: a) VEYA E5 endgame scaling.**
**SON (2026-07-18): Blok E3 connected/phalanx pawns (bağlı piyon) KABUL (kullanıcı kararı, erken
durdurma) -> BASELINE `2122456`, 177 test. SPRT base `75a5f17` (bad_bishop) vs new `2122456`
(bad_bishop + connected, connected'ı İZOLE eder): 4519 oyun, W-D-L 1287-2063-1169, Elo +9.1 ± 7.5,
LOS %99.1, LLR 2.26 (2.94'e YÜKSELİYORDU, kullanıcı erken durdurdu). Sub-5 koşullulardan (bad-bishop
+4/outpost +4.7, LLR ~1.2-1.7'de takılan) NİTELİKSEL FARKLI: +9.1 > elo1(5) -> LLR istikrarlı
tırmanıyordu, tek başına certify eden gerçek pozitif; erken-kabul king-safety `7eea85f` (LLR 1.46)
emsalinden güçlü, "H1 TAM KABUL" (2.94) DEĞİL ama sub-5 koşullulardan bir sınıf yukarı. Mekanik:
sıra-ölçekli yalnız-ilerlemiş (phalanx∨supported, bonus weight×max(0,göreli_sıra−2), 2/4 EG-ağır,
pawn cache). Enstrümantasyon ön-kayıtlı passed-örtüşme reddedildi (%0.1/%6.2). NOT: connected base
`75a5f17` üzerine certify etti -> bad_bishop connected'ın altında grandfathered (bağımsız certify
borcu 9c4f6d1-bundle'da). SIRADAKİ: E3 bitti; E4 rook-on-7th/bishop-pair rafinesi VEYA E5 endgame
scaling VEYA 9c4f6d1-bundle (kullanıcı kararı). NNUE cherry-pick ADAYI.**
**SON (2026-07-18): Blok E3 backward pawns DENENDİ, RAFA KALDIRILDI (SPRT H0 TAM RED). SPRT base
`167ade2` vs new `93321c9` (bad_bishop + backward BİRLİKTE): 3760 oyun, W-D-L 1011-1650-1099, Elo
−8.1 ± 8.3, LOS %2.8, LLR −2.95 TAM RED. bad_bishop tek başına +4 -> backward marjinali ≈ −12. Kod
`2122456`'da söküldü (connected + `pawn_attack_span` korundu); HEAD `2122456` = bad_bishop +
connected, 177 test; ağırlık=0 düğüm-eşitliği (bad_bishop+connected 0 -> d13 1121128/cp27 = `167ade2`).
**YENİ KRİTİK DERS: backward TÜM SPRT-ÖNCESİ KAPILARI GEÇTİ (üç soru ortogonal + enstrümantasyon
sağlıklı: netnz %14.6, NET |katkı|/çağrı mg 1.18 eg 1.77, sum/çağrı ~0 tarafsız, modest) ama net
−12. Kapılar ORTOGONALLİĞİ + KALİBRASYONU doğrular, heuristiğin İŞARETİNİN pratikte yardımcı olduğunu
DEĞİL.** Geri piyon güvenilir kötü değil (taş kompanzasyonu/bağlam); predicate iyi-ve-kötü geri
piyonları AYIRT ETMEDEN cezaladı — blockade işaret-tutarsızlığının sinsi hali: enstrümantasyon "tek
predicate daima −" der ama ateşlediği pozisyonların DEĞERİ değişken. Bireysel SPRT'nin bundle'dan
ÖNCE negatifi yakalaması sub-5 stratejisini doğruladı (bundle'a yalnız pozitif terim girer).**
**SON (2026-07-18): Blok E3 connected/phalanx pawns (bağlı piyon) KOD TAMAM, SPRT BEKLENİYOR ->
commit `7733d03` (backward sökülünce HEAD `2122456`), 177 test. Sıra-ölçekli YALNIZ İLERLEMİŞ varyant (protected-passer trap-2'sini
kesmek için): connected = phalanx (yatay komşu) VEYA supported (piyonca savunulan); bonus =
weight × max(0, göreli_sıra−2), 2-3. sıra 0; tapered EG-ağır 2/4. Saf-piyon -> pawn_structure_full
(cache); connected_pawns() izole helper. İki koruma: rr>=3 gate (düşük-sıra izole örtüşmesini keser)
+ "phalanx∨supported" ('sadece izole-değil'den dar). ENSTRÜMANTASYON: **ön-kayıtlı passed-örtüşme
RİSKİ REDDEDİLDİ** (passed-share %0.1 orta oyun / %6.2 oyun sonu); netnz %33.4, NET |katkı|/çağrı
mg 0.73 eg 1.47 (escort/outpost bandı), sum/çağrı ~0 (ölçek kaymıyor), oyun sonu eg 4.48 -> 2/4 iyi
kalibre. Kapılar: perft birebir, ağırlık=0 düğüm-eşitliği (3 terim 0 -> d13 1121128/cp27 = `167ade2`),
5 test (IgnoresUnsupportedAdvanced = ortogonallik kanıtı); PawnPassedBonus FEN gate-altına taşındı.
NNUE cherry-pick ADAYI. Ayrıntı: Blok E3 connected girişi.**
**SON (2026-07-18): Blok E3 backward pawns (geri piyon) KOD TAMAM, SPRT BEKLENİYOR -> commit
`93321c9`, 176 test. Tapered −8/−12: komşu VAR ama hepsi ileride (geriden destek yok) + durak
karesi rakip piyonca kontrol. Saf-piyon -> `pawn_structure_full` içinden çağrılır (cache'e girer);
`backward_pawns()` izole helper; `pawn_attack_span` engine ns'ine taşındı. ÜÇ SORU DA GEÇTİ
(ortogonal: izole/çift/geçer farklı, mobility piyonu saymaz). ENSTRÜMANTASYON (cache kapalı, her
eval): orta oyun netnz %14.6 / %1.19 piyon geri / NET |katkı|/çağrı mg 1.18 eg 1.77 (escort/outpost
bandı) / sum/çağrı ~0 -> −8/−12 kutu-dışı iyi kalibre (bad_bishop gibi kısmaya gerek YOK). Kapılar:
perft birebir, ağırlık=0 düğüm-eşitliği (bad_bishop+backward 0 -> d13 1121128/cp27 = `167ade2`),
4 test. SPRT base `167ade2` vs `93321c9`; sub-5 beklenir -> KOŞULLU + bundle. NNUE cherry-pick
ADAYI. Ayrıntı: Blok E3 backward pawns girişi.**
**SON (2026-07-18): Blok E4 bad bishop (kötü fil) KOŞULLU KABUL. SPRT base `167ade2` vs new
`75a5f17`: 10779 oyun, W-D-L 3022-4863-2894, Elo +4 ± 4.9, LOS %94.7, LLR 1.22 (2.94'e
sürünüyordu, kullanıcı durdurdu). OUTPOST DESENİNİN BİREBİR AYNISI (+4.7/LOS %96.8/LLR 1.72
@10k) -> sub-5 etki 0/5 sınırında certify EDİLEMEZ (Blok 1/3 Ders 3). Koşullu tutuldu, "H1
KABUL" DEĞİL; HEAD `75a5f17`, baseline yine `167ade2`; blok sonu BUNDLE SPRT'ye (base
`9c4f6d1`) girer. Tapered EG-ağır, fil-renginde dost piyon başına ceza + blokeli ek; E1
attack-pass'ine fold; yeni SquareColorMask; 4 skaler frozen sınır önüne (826->830). ÜÇ SORU
DA GEÇTİ (already-priced elle-kontrol: iyi-renk vs kötü-renk fil mevcut evalde yalnız ~2 cp,
işaret ters -> sinyal yeni). AĞIRLIK ENSTRÜMANTASYONLA 2/3/3/5 -> 1/2/2/3'e kısıldı (NET
|katkı|/çağrı mg 7->3.9 / eg 11->2.7; sum/çağrı ≈0 -> ölçek kaymıyor; %99.99 fil ateşliyordu).
Kapılar: 172 test, perft birebir, ağırlık=0 düğüm-eşitliği (startpos d13 1121128/cp27 =
`167ade2`). NNUE cherry-pick ADAYI (bundle H1 sonrası). Ayrıntı: Blok E4 bad bishop girişi.**
**SON (2026-07-17): Blok E3 rook-behind-passer (kale kendi geçer piyonunun arkasında) KABUL.
SPRT base `8b60653` vs new `167ade2`: 7741 oyun, W-D-L 2172-3564-2005, Elo +7.5 ± 5.7, LOS
%99.5, LLR 2.97 (2.94'ü GEÇTİ) TAM KABUL -> YENİ BASELINE `167ade2`, 167 test. E3'ün İKİNCİ
geçen terimi. EG-only, yalnız kendi kale: kendi kalemiz kendi geçer piyonumuzun arkasında
(aynı sütun + arada taş yok = `between_bb` temiz hat + göreli sıra≥3) ise `eg += 20`. Escort'un
pawn-cache passer kümesini kullandı (sıfır ek altyapı). Kapsam yalnız-kendi-kale (kullanıcı
kararı; rakip-kale rook_on_file ile örtüşürdü). Üç soru da geçti. Kapılar: perft birebir,
ağırlık=0 düğüm-eşitliği (escort+rook 0 -> startpos d13 1121128/cp27 = `47fada6`, sıfır
yan-etki), enstrümantasyon EG %8.7-42.8 / MG ~0 (frozen marjlar güvende) / per-fire ~20cp.
+7.5 > elo1(5) -> tek başına certify. YAN BULGU (ayrı commit `19b728c`, test-only): baseline'da
da olan şahsız FEN (`8/pp4pp/8/8/8/8/1P4P1/8`) Debug'da king_square array OOB veriyordu
(Release sessiz) -> şah eklendi. NNUE cherry-pick ADAYI.**
**SON (2026-07-17): Blok E3 geçer piyon şah eskortu (passer king escort) KABUL. SPRT base
`47fada6` vs new `8b60653`: 10483 oyun, W-D-L 3012-4649-2822, Elo +6.3 ± 5.0, LOS %99.4,
LLR 2.96 (üst sınır 2.94'ü GEÇTİ) TAM KABUL -> YENİ BASELINE `8b60653`, 163 test. Blok
E3'ün İLK geçen terimi (protected passer −22.1 + blockade −12.4 ard arda H0'dan sonra);
blockade'den kalan pawn-cache passer-kümesi altyapısını (pawn_structure_full) DOĞRULADI —
başarısız denemeden kalan yatırım bir sonraki terimi geçirdi. Mekanik: EG-only, her geçer
piyonun durak karesi için `eg += 8 * max(0, d_rakip_şah − d_kendi_şah)` (Chebyshev, cap 5,
sıra≥3), pawn-cache passed kümeleri. TASARIM HİKÂYESİ (S3 kapısı çalıştı): İLK simetrik
form (`w × (d_rakip − d_kendi)`, max'sız) GEOMETRİ yüzünden sistematik yanlıydı — durak
karesi rakip sahasında olduğundan kendi şah 25169 vakanın yalnız 5'inde (%0.02) daha yakın,
`d_e − d_o` sıra ile negatif korele -> `passed[r]`'nin negatif işaretli yeniden-ifadesi,
sum/çağrı −5.006 cp. Enstrümantasyon ALT-KÜME KIRILIMI (kendi-yakın vs rakip-yakın) bunu
SPRT'den ÖNCE yakaladı (blockade dersi: kırılım şart). DÜZELTME (kullanıcı kararı): tek-yönlü
`max(0,…)` -> taban sıfıra sabit, eskort BAŞARILDIĞINDA bonus, passed[] ile kavga etmez
(sum/çağrı −5.006 -> +0.632 cp; orta oyun ateşleme %6.69 -> %0.02 no-op; ağırlık 4 -> 8).
ÜÇ SORU DA GEÇTİ: (1) adıyla sayılmıyor; (2) işaret DEĞİŞKEN (kanat passer'ında ters, inşa
gereği bağ değil); (3) tek alt-kümede ateşler, orada işaret tartışmasız. +6.3 > elo1(5)
olduğundan LLR tavana tırmandı (outpost +4.7'de takıldı) -> bundle'a gerek kalmadı, tek
başına certify. NNUE cherry-pick ADAYI.**
**ÖNCESİ (2026-07-17): Blok E3 geçer piyon blokajı (blockade) DENENDİ, RAFA KALDIRILDI.
SPRT base `47fada6` vs new `af26f8f`: 2544 oyun, W-D-L 633-1187-724, Elo −12.4 ± 9.8,
LOS %0.7, LLR −2.95 TAM RED -> terim geri alındı (eval `47fada6`'ya birebir: startpos
d13 = 1121128/cp27/PV özdeş), 159 test. **YENİ (ÜÇÜNCÜ) HATA SINIFI: İŞARET-TUTARSIZ
PREDICATE — ortogonal olmak yetmiyor.** Kategori testi (adıyla+sonucuyla) GEÇTİ (piyon
mobilitesi eval'de yok, blokajcı PST'si piyondan habersiz, outpost'la örtüşmez) ama
predicate zıt işaretli iki alt-kümeyi tek sabitle cezalandırıyordu: at blokajı piyon
sahibinin aleyhine, KALE blokajı LEHİNE (kale pasifleşip piyona bağlanır — Tarrasch).
ÖN-KAYITLI TEŞHİS DOĞRULANDI: risk SPRT'den ÖNCE yazılmıştı, enstrümantasyon oyun
sonunda blokajların %84'ünün kale/vezir olduğunu ölçmüştü -> terim en çok ateşlediği
fazda yanlış işareti uyguluyordu. VARYANT EK SPRT'SİZ ELENDİ: "minör-only" gate'i mevcut
sayılardan %1.36 (MG) / %0.03 (EG) -> no-op eşiğinin altında => işaret-tutarlı alt-küme
çok nadir, yaygın olanın işareti yanlış => terim ölü. KORUNDU (latent altyapı):
**pawn cache artık geçer piyon KÜMESİNİ taşıyor** (PawnBucket 16->32 bayt, lockless-XOR
doğal genişleme; pawn_structure_full) — şah mesafesi + rook-behind-passer bunu aynen
kullanacak, EXACT ve maliyeti ~0. ÖLÇÜM MİMARİ KARARI VERDİ: küme yeniden üretilseydi
%3.21/%5.49 nps (~−10..−14 Elo) -> cache'e konunca −%0.73 (gürültü içinde). METODOLOJİ:
kısa benchmark −%1.96 diyordu, UZUN koşu (8.67M düğüm/~5 sn) −%0.73 dedi — nps iddiası
uzun koşu ister.**
**ÖNCESİ (2026-07-17): Blok E3 korunan geçer piyon (protected passer) DENENDİ, RAFA
KALDIRILDI. SPRT base `47fada6` vs new `5e302b6`: 1621 oyun, W-D-L 402-714-505, Elo
−22.1 ± 12.6, LOS %0, LLR −2.96 TAM RED -> eval kaynakları `47fada6`'ya birebir geri
alındı (düğüm 1121128/cp27/PV özdeş); 158 test (cache koruma testi latent olarak KORUNDU).
KÖK SEBEP YENİDEN-İFADE, sabit değil, ve kanıt İNŞA GEREĞİ: korunan geçer piyon TANIMI
GEREĞİ asla izole değil (koruyucu komşu sütunda olmak zorunda) -> izole terimi o yapıyı
zaten +30 EG ödüllendiriyordu (c4+d5 zinciri 95 vs a4+d5 ayrık 65, EG); +18 eklemek
farkı %60 büyüttü, sıfır yeni bilgiyle. DERSİN RAFİNESİ (EN KRİTİK DERS'e işlendi):
sinyali ADIYLA değil SONUCUYLA ara — "korunan geçer piyonu bilen terim var mı?" doğru
cevaplandı ama YANLIŞ SORUYDU; doğrusu "korunan olmanın SONUCU zaten fiyatlanmış mı?".
İKİ KAPI DA YETERSİZ: enstrümantasyon ortogonalliğe KÖR ("SAĞLIKLI" dedi: oyun sonu %20
ateşleme / 2.14 cp; yine −22), ağırlık=0 düğüm-eşitliği yalnız yan-etki yokluğunu kanıtlar.
ANOMALİ SİNYALİ (kaçırıldı): ~2 cp ortalama katkılı terim −22 Elo veremez.**
**ÖNCESİ (2026-07-16): Blok E4 outpost `47fada6` KOŞULLU KABUL. SPRT base `9c4f6d1`:
10.000 oyun TAVAN, W-D-L 2749-4638-2613, Elo +4.7 ± 5.0, LOS %96.8, LLR 1.72 (2.94'ün
%58'i, yükseliyordu), beraberlik %46.4, renk 5000/5000. SABIR DERSİ: 1319 oyunda tablo
−0.3 ± 13.9 / LOS %48.5 idi ve "nötr" görünüyordu — ama beklenti bandı (+3-12) hata
payının TAMAMEN içindeydi, yani hipotez test edilmemişti; 10k'da +4.7/LOS %96.8 (LMP
emsali: 247 oyunda düz, +34.5 bitti). YENİ TEKNİK: **ağırlık=0 hilesi** — sezgisel eval
teriminin ağırlıklarını EvalFile ile 0 yap -> eval baseline ile sayısal özdeş -> düğüm
BİREBİR çıkmalı (962829/714878 birebir çıktı) => yan-etki kapısı + temiz nps ölçümü
(kirli −%4.8 yerine gerçek ~%1.5). ÇÜRÜTÜLEN TAHMİN: "terim EG'de şişer" YANLIŞ —
ateşleme fazlar arası düz (%2.49/%2.70), çünkü kovulabilirlik gevşerken (rank_ok
%27->%52) piyon desteği zorlaşıyor (support_ok %47->%20). Rook-on-7th kategori gereği
ERTELENDİ (kale PST'si zaten 7. sıraya +10 -> kısmen yeniden-ifade).**
**EN KRİTİK DERS (2026-07-16): ORTOGONAL sinyal GEÇER; zaten-yakalanan sinyalin
YENİDEN-İFADESİ ya da FROZEN kalibre alt-sisteme (SafetyTable / cp marjları) enjeksiyon
KALIR. Yeni terimde ÖNCE sor: "bu sinyal başka yerde sayılıyor mu / kalibre-dondurulmuş
sisteme mi giriyor?" -> EVET ise ortogonal terime geç ya da E7 joint tune. H0'ı sabit
düzeltip retry ETME (kategori sorunu). Ayrıntı: Faz 3 metodoloji girişi.**
**SON: Blok E2 iki king-safety rafinesi RAFA (kategori kapanışı): (a) king-on-open-file
`9c62fe6` -> net H0 (1533 oyun, Elo −11.9 ± 12.7), revert `d7963d2`; (b) piyon-king-ring
`3fc3d73` -> ~nötr/H0 (639 oyun, W-D-L 170-284-185, Elo −7.3 ± 20.3, LOS %24.1, LLR
−0.454), revert `cce222c`. İkisi de `9c4f6d1`'e birebir, threats korundu, 152 test.
Kalan king-safety (pawn storm/safe check/attacker-count) -> E7 joint safety tuning.**
**SON: Blok E2 Commit 1 — threats / hanging (tam aile) KABUL. YENİ BASELINE `9c4f6d1`,
152 test. Dört alt-terim (ThreatByPawn 40/30, ThreatByMinor 25/20 [minör→majör],
ThreatByRook 20/15 [kale→vezir], Hanging 25/25 [savunmasız]), E1'in birleşik attack-
pass'ine fold (mobility_king_safety_impl -> attack_eval_impl; by_pawn/by_minor/by_rook/
by_piece aggregate, by_all ile hanging). Heuristik -> kapı SPRT; ölçek modest (8 skaler
frozen sınır önüne, eval_frozen_start 812->820). SPRT base `5dbc2ff` vs new `9c4f6d1`:
1-thread H1 TAM KABUL (kanonik), 2-thread+büyük Hash ile de doğrulandı; tam istatistik
kaydedilmedi (test kapatıldı). DERS: ilk 4-thread koşusu H0 -> SMP bug DEĞİL (entegrasyon
incelendi, tam thread izolasyonu; bkz. memory `sprt-gui`) -> OVERSUBSCRIPTION (2 motor ×
4 thread > 14 çekirdek). Thread↑ -> eşzamanlılık↓ + Hash↑. NNUE cherry-pick ADAYI.**
**ÖNCESİ: Blok E1 (Eval altyapısı+hız) TAM TAMAM — iki EXACT commit, baseline
`5dbc2ff`, 148 test. (a) Pawn hash table `201e9d8` (global lockless-XOR pawn cache;
Board::pawn_key incremental; g_pawn_cache_enabled tuner'da kapalı; nps +%2.0/+%5.1).
(b) Birleşik attack-pass `5dbc2ff` (mobility+king_safety tek geçiş, aynı N/B/R/Q atak
setleri; mobility()/king_safety() ince sarmalayıcı impl'e delege; nps +%2.4/+%2.2).
İkisi de node-equality birebir (startpos d13 609707, Kiwipete d12 549629). Kabul kapısı
EXACT (SPRT'siz; TT-prefetch/seldepth/MDP emsali). Değer E3'te katlanır; NNUE cherry-pick
ADAYI (search'e dokunmaz). ÖNCESİ: SMP iyileştirme paketi — TT prefetch (`01853a0`, EXACT) + 4-yollu bucket TT
(`ee266e5`, heuristik) KABUL (ikili SPRT: 1v1 H1 → depth-skip 1 thread'te inert
olduğundan prefetch+4-way'e atfedildi); depth-skipping (`466b6f6`) RAFA (`1f497dc`
revert; 4v4 nötr, ölü ağırlık). YENİ BASELINE `1f497dc`, 142 test. DERS: 1-thread
kazancı N-thread'te yıkanabilir; bir bileşen bir thread sayısında inert ise o koşuyu
diğerlerine atfetmek için kullan. Fork: prefetch+4-way N4'ten önce NNUE'ya cherry-pick.**
FAZ 2D (Lazy SMP) dört commit (hepsi main): `19d4a04` thread-safe TT (lockless XOR, tek-thread
EXACT), `a967672` SMP orchestrator + per-thread tablolar + Threads option (threads=1
EXACT), `e810a9a` tools/sprt Threads desteği, `36cb6d5`+GUI guard düzeltmesi. 141 test.
threads=1 düğüm eşitliği baseline `aaaec37` ile birebir (startpos d13 780013, Kiwipete
d12 488482) -> davranış-koruyan. **SCALING SPRT GEÇTİ: 2 thread vs 1 thread (aynı kod)
Elo +88.7 ± 24.5, LOS %100, LLR 2.95 TAM KABUL (424 oyun, 168-194-62) -> Lazy SMP
gerçekten Elo veriyor.** Fork kısıtı: thread-safe TT + orchestrator (`19d4a04`+`a967672`)
NNUE'ya N4'ten (incremental accumulator) ÖNCE cherry-pick edilmeli (`search.cpp`
değişmediği için TT commit'i çakışmasız). Opsiyonel ileri: 2→4 thread ölçekleme, TT
prefetch/4-yollu bucket, depth-skipping ile daha iyi ıraksama. Önceki EN GÜNCEL
(Faz 2C-devam sonu): Blok 3/13 countermove yumuşak history-bonusu (retry)
TAMAM, SPRT +10.4 ± 7.2 Elo TAM KABUL (4602 oyun) -> BASELINE `aaaec37`, 138 test.
FAZ 2C-devam Blok 1/1 (pin-aware Aşama 2, +39.7 Elo) + Blok 1/2
(TT yenileme paketi) TAMAM. Blok 1/2 iki SPRT: SPRT-A (B1..B4) +12.9 ± 8.2 Elo TAM
KABUL, SPRT-B (B5 qsearch TT) +33 ± 14.1 Elo TAM KABUL — toplam ~+46 Elo, beklentiyi
(+10-25) aştı. YENİ BASELINE `23d28b0`.
Blok 1/3 (eval stack + improving) DENENDİ, RAFA KALDIRILDI: ilk sürüm SPRT H0,
kalibre revize ~NÖTR (+1.2 ± 5.3, 10k tavan) → `23d28b0`'a birebir geri alındı
(bkz. Blok listesi madde 3; check extension emsali).
Blok 2/4 (singular extension) TAMAM: SPRT +21.3 ± 11.1 Elo, LLR 2.94 TAM KABUL
(2274 oyun, LOS %100). YENİ BASELINE `a803a3f`.
Blok 2/5 (SEE paketi, main search) TAMAM: İKİ COMMIT/İKİ SPRT, toplam ~+56 Elo.
Commit 1 (kayıplı yakalama sıralaması, `dd9e8f3`) +33.1 ± 14.1 Elo TAM KABUL (1420
oyun); Commit 2 (sığ-derinlik SEE budaması, `8fa2281`) +23.3 ± 11.7 Elo TAM KABUL
(1972 oyun). Beklenti (+15-30) net aşıldı. YENİ BASELINE `8fa2281`.
Blok 2/6 (capture history) DENENDİ, RAFA KALDIRILDI: SPRT H0, -16.2 ± 11 Elo, LLR
-2.95 TAM RED (2236 oyun, LOS %0.2) -> `src/search.cpp` `8fa2281`'e birebir geri
alındı. KRİTİK DERS: düğüm sağlaması NET İYİYDİ (agregat 0.83×, Kiwipete 0.64× =
DAHA AZ düğüm) ama Elo -16 -> sezgisel yeniden-sıralamada düğüm ≠ Elo (kur yalnız
EXACT hızlanmalara; bkz. Blok listesi madde 6).
Blok 2/7 (IIR — Internal Iterative Reduction) TAMAM: SPRT +15 ± 9 Elo, LLR 2.96 TAM
KABUL (3299 oyun, 1021-1399-879, LOS %99.9). Base `8fa2281` vs new `3bde658`. Beklenti
(+5-15) üst ucu. ply>0 + depth>=kIirMinDepth(4) + tt_move yoksa depth-1 (null-move'dan
sonra). IIR arama davranışını TT-durumuna bağımlı kılan İLK sezgisel -> 2 TT testi
(ResultConsistentAcrossTTState, ReducesNodesOnResearch) yeniden yazıldı. Düğüm sağlaması
KARIŞIK (startpos d13 +%48, Kiwipete d12 -%13) ama Elo +15 -> düğüm≠Elo dersi tekrar.
Baseline `3bde658` oldu.
Blok 2/8 (history-tabanlı quiet budaması) TAMAM: SPRT +14.3 ± 8.7 Elo, LLR 2.96 TAM
KABUL (3575 oyun, 1129-1464-982, LOS %99.9). Base `3bde658` vs new `a8ac0d9`. Beklenti
(+10-20) tam ortası. Sığ, çekte-olmayan düğümde ham `stat = main + cont < -2000*depth`
olan quiet, çek-vermeyen late hamleler (moves_searched>0) budanır — SEE'den önce.
Sabitler ölçülerek seçildi (gate %2.06 buduyor, no-op değil). 2 test (122->124).
YENİ BASELINE `a8ac0d9`.
Blok 2/9 (null move güçlendirme) KISMEN TAMAM: iki commit/iki SPRT. Commit 1
(`eval>=beta` kapısı + dinamik R, `a2a6bfa`) KABUL: SPRT +21.9 ± 11.3 Elo, LLR 2.95
TAM KABUL (2032 oyun, 629-902-501, LOS %100) — tek başına Blok 2/9 beklentisini
(+5-15) aştı. Commit 2 (verification search, `3f0f5ac`) DENENDİ/RAFA: SPRT NÖTR
(+1.8 ± 9.6, LLR -0.136, 2654 oyun) -> `a2a6bfa`'ya birebir geri alındı (`f3f48e6`).
DERS: kNullVerifyMinDepth=12 hızlı TC'de nadir tetikler (check extension + Blok 1/3
Ders 3 emsali; elo0=0/elo1=5 ile ~0 etki 10k'ya sürünür). YENİ BASELINE `a2a6bfa`.
Blok 3/11 (adaptif zaman yönetimi — best-move stability) TAMAM: SPRT +40.6 ± 15.8
Elo, LLR 2.96 TAM KABUL (1022 oyun, 341-459-222, LOS %100). Base `a2a6bfa` vs new
`a44d6eb`. Beklenti (+5-20) NET AŞILDI — hızlı SPRT TC'sinde (5+0.05) zaman yönetimi
büyük kaldıraç ("hızlı TC'de belirgin" öngörüsü doğrulandı). Kök best-move stability
soft limiti ölçekler (`time_scale`, [0.5,1.5]); yalnız timed-game modu (adaptive_time
bayrağı), movetime/depth/infinite dokunulmaz. Fail-low uzatması ertelendi. 2 test
(125->127). YENİ BASELINE `a44d6eb`.
Blok 3/12 (tempo bonusu) DENENDİ, RAFA KALDIRILDI: `TempoBonus=15` (`71127ce`) SPRT
H0 (-18.6 ± 13 Elo, LLR -2.41, LOS %0.2, 1399 oyun) -> `a44d6eb`'ye birebir geri
alındı (revert `149cc92`). DERS: tempo `evaluate()` içinde olunca budama kapılarının
`static_eval`'ini de kaydırır -> RFP (`return static_eval`) over-prune + şişik
fail-high; elle-kalibre marjlar tempo'suz ayarlandığından denge bozuldu (improving/
capture-history emsali). İleride Blok 4 Texel sonrası ya da cerrahi varyant (tempo
yalnız yaprak/qsearch, budama tempo'suz).
Blok 3/13 (fırsat işleri) KISMEN: hijyen paketi (ikisi de SPRT'siz) TAMAM ->
seldepth raporlama (kozmetik, EXACT, `caf663a`) + mate distance pruning (near-exact,
`9307674`). seldepth arama davranışını hiç değiştirmez; MDP non-mate sonuçlara
dokunmaz (a44d6eb'a karşı startpos d13 668137 / Kiwipete d12 715497 birebir), yalnız
mat hatlarında budar (iki-kale matı d4 1357->662). Ardından **delta pruning (qsearch)
TAMAM: SPRT +7.8 ± 5.8 Elo, LLR 2.96 TAM KABUL (7192 oyun, 1989-3375-1828, LOS %99.5).
Base `a44d6eb` vs new `94c5b95`. YENİ BASELINE `94c5b95`, 131 test.** Çekte değilken,
promosyon-olmayan yakalama için `raw_eval + MaterialValue[victim] + kDeltaMargin(200)
<= alpha` -> `continue` (çalışan alpha ile, güvenli continue). Beklenti (+5-15) tam
ortası, mütevazı etki -> çok oyun (razoring deseni). Perft birebir; düğüm sağlaması
startpos d13 -%7.6 / Kiwipete d12 -%14.4 (bestmove makul).
BLOK 4 (Texel) ALTYAPI TAMAM ama İLK TUNING ~NÖTR (2026-07-12): 14 datagen `5e64fd4`,
15a EvalParams refactor `2742583` (EXACT), 15b/c tuner `29ddeaa` + EvalFile `3dfc3a9`,
tuner-sertleştirme `1343b74`. İki SPRT: tune-tümü **-110 Elo REGRESYON** (eval ölçeği
şişip cp arama marjlarını bozdu; ölçek hipotezi), material-freeze+reg **~NÖTR** (-9.3 ±
19.4). Baseline `94c5b95` DEĞİŞMEDİ. Tuner + datagen NNUE'ya da hizmet eder.
Blok 3/13 countermove yumuşak history-bonusu (retry) TAMAM: SPRT +10.4 ± 7.2 Elo, LLR
2.96 TAM KABUL (4602 oyun, 1242-2256-1104, LOS %99.8). Base `94c5b95` vs new `aaaec37`.
Eski sert-bant (-60 Elo, `875d5f5`) yerine quiet skoruna eklenip history bandına kırpılan
yumuşak bonus; cont_hist örtüşmesine rağmen net pozitif (mütevazı -> çok oyun). YENİ
BASELINE `aaaec37`, 138 test. Blok 3/13 kök hamle sıralaması (önceki iterasyon düğüm
sayıları) DENENDİ, RAFA KALDIRILDI: SPRT H0 yönünde (kullanıcı erken durdurdu -16.7 ±
22.7 Elo, 480 oyun, LOS %7.5), commit `4612c5c` -> revert `3ae1331` (`aaaec37`'ye birebir,
testler 138). DERS: "kökte budama yok -> exact" varsayımı YANLIŞ (alt-ağaç TT/history
yol-bağımlılığı sonucu değiştirir; sezgisel). Motorun tt+MVV+history kök sıralaması zaten
TT/history ile senkron; node-count enjekte etmek geri-beslemeyi bozup verimsizleştirdi
(gate'siz startpos +%62 düğüm; kRootNodeOrderMinDepth=6 ile düzeldi ama SPRT yine negatif).
Node-count = gürültülü kalite vekili; döviz kuru UYGULANAMAZ (sonuç değişiyor). SIRADAKİ:
Blok 3/13 KAPANDI (tüm kalemler tamam ya da rafta). Blok 2 opsiyonel (ProbCut, singular
multicut), Blok 4 tuning için daha derin/çeşitli veri ya da joint arama-marj tuning
(madde 16), ya da Faz 2D (tüm bloklar bitince) ileride adaydır. İleride kök sıralaması
adayı: önceki-iterasyon SKORU'na göre (Stockfish-tarzı, node-count değil).
Blok 2 kalanı opsiyonel: Blok
2/10 ProbCut, Blok 2/4 multicut. Capture history + IIR-tuning (kIirMinDepth 6-8 /
reduce-by-2) + null verification (düşük kNullVerifyMinDepth ya da fail-soft null) +
tempo (Texel sonrası/cerrahi) + adaptif zamanın fail-low uzatması ileride yeniden
denenebilir. FAZ 2D tüm bloklar bitince.**
Proje fork'landı: NNUE işi `../ChessEngineNNUE`'da. **NNUE CHERRY-PICK BORCU KAPANDI
(2026-07-12):** tüm kabul edilmiş kod backlog'u (Blok 1/2 `1d73725..23d28b0`, Blok 2/4
`a803a3f`, Blok 2/5 `dd9e8f3..8fa2281`, Blok 2/7 `3bde658`, Blok 2/8 `a8ac0d9`, Blok 2/9
Commit 1 `a2a6bfa`, Blok 3/11 `a44d6eb`, Blok 3/13 `caf663a`+`9307674`) NNUE reposuna
kronolojik sırayla, ÇAKIŞMASIZ cherry-pick edildi (14 commit) ve doğrulandı: 129/129
test, düğüm-eşitliği (startpos d13 668137 / Kiwipete d12 715497 klasik HEAD ile birebir).
NNUE origin/main = `51ce064`. Revert edilen deneyler (Blok 1/3, 2/6, 2/9-C2, 3/12) ve
"Yol haritası" commit'leri (NNUE kendi CLAUDE.md'sini taşır) taşınmadı. Fork kısıtı:
Faz 2D commit'leri N4'ten önce cherry-pick edilmeli.
**CHERRY-PICK BORCU TAM KAPANDI (2026-07-13): Blok 3/13 delta pruning `94c5b95` +
countermove `aaaec37` NNUE'ya cherry-pick edildi (çakışmasız) -> NNUE origin/main =
`fc3b082`, 133 test.** DİKKAT: 2026-07-12 "borç kapandı" notu delta pruning'i (`94c5b95`,
o an henüz kabul edilmemişti) ATLAMIŞTI — CLAUDE.md yalnız `aaaec37`'yi borç işaretledi,
delta gözden kaçtı. Bu oturumda ikisi de taşındı (delta önce cherry-pick edildi ama
NNUE'ya countermove'dan SONRA uygulandı; ikisi bağımsız bölge [qsearch vs move ordering]
olduğundan sıra final kodu etkilemez, klasik `aaaec37` ile eşdeğer). **DÜĞÜM-EŞİTLİĞİ
DOĞRULANDI (ikisi de taşındıktan sonra): NNUE `fc3b082` search.cpp == klasik `aaaec37`
search.cpp BİREBİR (diff boş); startpos d13 780013/cp22/b1c3 + Kiwipete d12 488482/cp-76/
d5e6 klasik ile PV dahil özdeş.** 133 test. NOT: eval ıraksamadı — NNUE işi henüz
başlamadı; kaynak farkı yalnız Blok 4 Texel altyapısı (datagen/tuner/eval_params +
eval.cpp EXACT refactor `2742583`, tuning adopte EDİLMEDİ -> eval değerleri özdeş),
search'e dokunmuyor. (İlk ölçümde görülen fark, delta pruning HENÜZ taşınmamışken alınmıştı;
delta portlanınca kayboldu.) Kök hamle sıralaması (`4612c5c`, revert) NNUE'ya taşınmadı
(net-sıfır deney).
Motor UCI üzerinden GUI'ye bağlanıyor, legal oynuyor, perft geçiyor. Toplam 138
test geçiyor. Faz 2B (gelişmiş evaluation + SPRT/maç altyapısı) tamamlandı; tüm
eval terimleri SPRT'den geçti. Faz 2C selective search: PVS + null move + SEE +
LMR + futility ailesi + LMP + razoring TAMAM (hepsi SPRT'den geçti). Check extension
(naif + SEE-kapılı) DENENDİ ama SPRT'de NÖTR -> rafa kaldırıldı. Move ordering
güçlendirme: **history malus SPRT +22.2 Elo kabul**, ardından **continuation history +
tablo kalıcılığı paketi SPRT +31.6 ± 13.8 Elo, LLR 2.96 tam kabul**, ardından
**history-tabanlı LMR + ölçek çarpanı SPRT +13.6 ± 8.5, LLR 2.95 tam kabul (3928 oyun)**.
**FAZ 2C-ek KAPANDI. Yeni baseline `9bdcef4`.** Countermove (regresyon) denendi, geri
alındı. **SIRADAKİ İŞLER (fork sonrası, klasik tabanda): (1) Faz 2C-devam Blok 1/1
(Aşama 2 movegen) TAMAM, SPRT +39.7 -> `d07e7f2`; Blok 1/2 TT yenileme ~+46 ->
`23d28b0`; Blok 1/3 improving DENENDİ/RAFA (H0 + kalibre nötr) -> `23d28b0`; Blok 2/4
singular extension TAMAM, SPRT +21.3 -> `a803a3f`; Blok 2/5 SEE paketi TAMAM, iki
commit/iki SPRT ~+56 (Commit 1 +33.1 -> `dd9e8f3`, Commit 2 +23.3 -> `8fa2281`);
Blok 2/6 capture history DENENDİ/RAFA (SPRT H0 -16.2, düğüm iyi ama Elo negatif ->
`8fa2281`); Blok 2/7 IIR TAMAM, SPRT +15 -> `3bde658`; Blok 2/8 history-tabanlı quiet
budaması TAMAM, SPRT +14.3 -> `a8ac0d9`; Blok 2/9 null move güçlendirme KISMEN TAMAM
(Commit 1 `eval>=beta`+dinamik R KABUL SPRT +21.9 -> `a2a6bfa`; Commit 2 verification
search DENENDİ/RAFA SPRT nötr +1.8 -> `a2a6bfa`'ya geri alındı `f3f48e6`); Blok 3/11
adaptif zaman yönetimi TAMAM, SPRT +40.6 -> `a44d6eb`; Blok 3/12 tempo DENENDİ/RAFA
(H0 -18.6) -> `a44d6eb`; Blok 3/13 hijyen paketi (seldepth+MDP, SPRT'siz) + delta
pruning TAMAM, SPRT +7.8 -> `94c5b95` (YENİ BASELINE); sıradaki Blok 3/13 kalan
(countermove history-bonusu, kök hamle sıralaması) -> Blok 4 Texel tuning. Blok 2
kalanı opsiyonel [ProbCut + singular multicut], (2) Faz 2D Lazy SMP (tüm bloklar
bitince, NNUE'ya N4'ten önce cherry-pick).** Proje iki repoya ayrılıyor
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
kaldıraç. GÜNCELLEME (2026-07-10): artık PLANDA — Faz 2C-devam Blok 4 (Texel
tuning); veri pipeline'ı NNUE veri toplamaya da hizmet edecek.

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
SIRADAKİ: Faz 2C-devam (tek-thread güçlendirme; Blok 1/1 Aşama 2 movegen TAMAM,
SPRT +39.7 -> `d07e7f2`; Blok 1/2 TT yenileme ~+46 -> `23d28b0`; Blok 1/3 improving
DENENDİ/RAFA [H0 + kalibre nötr, `23d28b0`]; Blok 2/4 singular extension TAMAM, SPRT
+21.3 -> `a803a3f`; Blok 2/5 SEE paketi TAMAM, iki commit/iki SPRT ~+56 -> baseline
`8fa2281`; Blok 2/6 capture history DENENDİ/RAFA [SPRT H0 -16.2, düğüm iyi/Elo negatif,
`8fa2281`]; Blok 2/7 IIR TAMAM, SPRT +15 ± 9 -> `3bde658`; Blok 2/8 history-tabanlı
quiet budaması TAMAM, SPRT +14.3 ± 8.7 -> `a8ac0d9`; sıradaki Blok 2/9 null move
güçlendirme) -> sonra Faz 2D (Lazy SMP,
tüm bloklar bitince).** Tapered eval (+42.8), pawn structure
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
