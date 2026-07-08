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

- [x] Zobrist hashing + transposition table
- [x] Move ordering: MVV-LVA, killer moves, history heuristic
- [x] Quiescence search (horizon effect'i azaltmak için)
- [x] Iterative deepening + time management
- [ ] Gelişmiş evaluation: pawn structure, king safety, piece mobility
- [ ] Cutechess-cli ile otomatik maç serisi altyapısı kur, her değişikliği
      önceki versiyona karşı test et (SPRT mantığıyla — regresyon var mı yok mu)

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
geçiyor. Toplam 51 test geçiyor.

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

**Sıradaki: Faz 2 Adım 5 — Gelişmiş evaluation** (pawn structure, king safety,
piece mobility). Sonra cutechess-cli ile otomatik maç/SPRT regresyon altyapısı.

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
