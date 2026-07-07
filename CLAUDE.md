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
- [ ] Move generation: piyon, at, fil, kale, vezir, şah + özel hamleler
      (rok, en passant, promotion)
- [ ] Magic bitboards ile sliding piece (fil/kale/vezir) hareket üretimi
- [ ] **Perft testleri**: standart perft pozisyonlarına (Kiwipete dahil) karşı
      derinlik 5-6'ya kadar doğrula, sonuçları bilinen referans değerlerle karşılaştır
- [ ] Basit minimax + alpha-beta pruning
- [ ] Materyal + piece-square table tabanlı basit evaluation
- [ ] UCI protokolü implementasyonu (Cutechess/Arena ile test edilebilir olsun)

Faz 1'in bitiş kriteri: motor UCI üzerinden bir GUI'ye bağlanabiliyor, legal
hamlelerle oynuyor, perft testleri geçiyor.

### Faz 2 — Klasik Güçlendirme

- [ ] Zobrist hashing + transposition table
- [ ] Move ordering: MVV-LVA, killer moves, history heuristic
- [ ] Quiescence search (horizon effect'i azaltmak için)
- [ ] Iterative deepening + time management
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

**Güncel durum (2026-07-07):** Faz 1, Adım 1 TAMAM. CMake + C++20 iskeleti kuruldu;
bitboard tabanlı `Board` representation (LERF kare eşlemesi, taşları türe + renge
göre tutan çift temsil), UTF-8 figürinlerle tahta yazdırma ve GoogleTest smoke
testleri (4/4 geçiyor) hazır. Derleme MSVC + Ninja ile uyarısız.

**Sıradaki:** Faz 1, Adım 2 — move generation (önce piyon/at/şah + rok/en passant/
promotion, sonra magic bitboard'larla sliding taşlar), ardından perft testleri.
Not: `lsb/msb/popcount` intrinsic sarmalayıcıları henüz eklenmedi, move
generation'da eklenecek.

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
