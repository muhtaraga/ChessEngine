# Satranç Motoru Projesi

Ben bir satranç motoru geliştiriyorum, hedefim Stockfish'in izlediği klasik yoldan
(alpha-beta search) başlayıp zamanla NNUE tabanlı evaluation'a geçmek. Performans
kritik olduğu için **C++** kullanacağız.

Bu bir tek seferlik kod üretimi değil, aylar sürecek iteratif bir proje. Sana rehberlik
edecek prensipler ve yol haritası aşağıda. Her oturumda bu bağlamı hatırla, ilerlemeyi
bu fazlara göre değerlendir.

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

**Faz 2B — Değerlendirme + test altyapısı (sıradaki, ön koşul)**

- [ ] Gelişmiş evaluation: pawn structure (isole/çift/geçer piyon), king safety
      (şah bölgesi saldırıları, piyon kalkanı), piece mobility, bishop pair,
      rook on open/semi-open file. Tapered eval (middlegame/endgame interpolasyon).
- [ ] **cutechess-cli ile otomatik maç + SPRT regresyon altyapısı.** Bir betik:
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

- [ ] **PVS (Principal Variation Search)**: ilk hamle tam pencereyle, kalanlar
      null-window [α, α+1] ile aranır; α'yı geçen olursa tam pencereyle yeniden
      aranır. LMR'nin oturduğu çerçeve — önce bu gelmeli.
- [ ] **Null move pruning**: rakibe bedava hamle ver, azaltılmış derinlikte
      (R≈2-3) beta etrafında ara; sonuç ≥ beta ise dalı buda. **Dikkat:** çekteyken,
      çok sığ derinlikte ve zugzwang riskinde (yalnız şah+piyon kalınca) uygulanmaz;
      yüksek derinlikte verification search ile zugzwang blunder'ı önlenir.
- [ ] **SEE (Static Exchange Evaluation)** + quiescence temizliği: bir karedeki
      taş alışverişinin materyal sonucunu hesapla. Kullanımı: (a) qsearch'te kayıplı
      yakalamaları ele, (b) delta pruning (Adım 3 notundaki ertelenen işler), (c)
      ileride LMR/futility kararlarında "taktik olarak kazançlı hamleyi budama".
- [ ] **LMR (Late Move Reductions)**: move ordering'de geç gelen, quiet, çek
      olmayan hamleleri azaltılmış derinlikte ara (derinlik+sıraya bağlı log tablo);
      α'yı geçerse tam derinlikte yeniden ara. **Tek başına en büyük Elo kazancı**
      ve nominal derinliği en çok şişiren teknik (Stockfish'in bizden hızlı
      derinleşmesinin ana sebebi).
- [ ] **Futility + reverse futility (static null move)**: yaprağa yakın, static
      eval ± margin ile umutsuz quiet hamleleri/dalları ele.
- [ ] **Late move pruning (move-count)**: sığ derinlikte, belli sayıdan sonraki
      quiet hamleleri hiç arama.
- [ ] **Razoring**: sığ derinlikte eval çok geriyse doğrudan qsearch'e düş, o da
      α'nın altındaysa buda.
- [ ] **Check extension** (çek veren/çekteki hatları uzat). İleride opsiyonel:
      **singular extension** (bir hamle diğerlerinden belirgin iyiyse uzat) — ileri
      seviye, en sona.

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

**Güncel durum (2026-07-08): FAZ 1 TAMAMLANDI, FAZ 2 devam ediyor.** Motor UCI
üzerinden GUI'ye bağlanabiliyor, legal hamlelerle oynuyor, perft testleri
geçiyor. Toplam 59 test geçiyor.

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
  - Sıradaki 2B işleri: pawn structure (isole/çift/geçer), king safety, piece
    mobility, bishop pair, rook on open/semi-open file; sonra cutechess-cli + SPRT.

**Sıradaki: Faz 2B** — önce gelişmiş evaluation (pawn structure, king safety,
piece mobility, tapered eval), ardından cutechess-cli + SPRT maç altyapısı. SPRT
altyapısı bir kez oturunca Faz 2C (selective search: PVS, null move, SEE, LMR,
futility ailesi, LMP, razoring, extensions) sırayla, her biri ayrı SPRT'den
geçirilerek eklenir. Faz 2D (Lazy SMP multi-threading) klasik fazın son adımı,
NNUE'dan önce. Yol haritası detayı için "Faz 2 — Klasik Güçlendirme" bölümüne bak.

### İlk somut görev

Faz 1, adım 1'den başlayalım: proje iskeletini kur (CMake ile, C++20 standardı
tercih edilir), temel bitboard struct'ını ve board representation'ı oluştur,
boş bir board'u UTF-8 satranç sembolleriyle terminale basan basit bir debug
fonksiyonu yaz. Bundan sonraki adımda move generation'a geçeceğiz.

### Notlar

- Proje ilerledikçe "Şu an nerede olduğumuzu takip et" bölümünü elle güncelleyip
  commit'le, böylece her yeni oturum güncel durumu bilir.
- Perft referans değerleri için Chess Programming Wiki'deki "Perft Results"
  sayfası standart kaynak; ilgili pozisyonları ve derinlik değerlerini kontrol
  ederken oradan doğrula.
