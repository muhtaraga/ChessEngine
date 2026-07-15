#pragma once

// Pawn hash table: yalnız piyon diziliminin Zobrist alt-anahtarıyla (Board::pawn_key)
// indekslenen küçük bir cache. pawn_structure()'ın (izole/çift/geçer) beyaz-bakışı
// MG/EG çıktısını saklar -> aynı piyon yapısına farklı taş dizilimleriyle tekrar
// tekrar ulaşıldığında (arama boyunca çok sık) yeniden hesaplama elenir.
//
// Cache = saf fonksiyonun (sabit g_eval için) memoizasyonu -> tek-thread davranış
// BİREBİR korunur (EXACT). Yalnız hız değişir.
//
// Yuva TT ile aynı lockless-XOR (Hyatt) desenidir ama sadeleştirilmiş: küme yok,
// yaşlandırma yok, derinlik yok (piyon eval'i derinlikten bağımsız). Boyut sabit
// (UCI Hash'e bağlı değil). Global tek örnek, thread'ler arası paylaşılır.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace engine {

// Lockless-XOR piyon yuvası (TT Bucket deseni). data düşük 32 bit'e mg|eg paketler
// (her biri int16). key = pawn_key XOR data. Yırtık okumada (k ^ d) != pawn_key ->
// miss (bozuk giriş kullanılmaz). Tek thread'te round-trip kayıpsız -> EXACT.
struct alignas(16) PawnBucket {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::uint64_t> data{0};
};

class PawnTable {
public:
    PawnTable() { clear(); }

    // pawn_key için giriş varsa mg/eg'yi doldurur ve true döner; yoksa false.
    bool probe(std::uint64_t pawn_key, int& mg, int& eg) const;

    // Girişi saklar (daima ezer; derinlik/yaş yok).
    void store(std::uint64_t pawn_key, int mg, int eg);

    // Tüm girişleri sıfırlar (ucinewgame, eval param değişimi).
    void clear();

private:
    // 2'nin kuvveti yuva sayısı; index = pawn_key & kMask. 2^16 yuva x 16 bayt = 1 MB.
    static constexpr std::size_t kBits = 16;
    static constexpr std::size_t kCount = std::size_t(1) << kBits;
    static constexpr std::size_t kMask = kCount - 1;

    // mg (int16) alt 16 bit, eg (int16) sonraki 16 bit; üst 32 bit 0.
    static std::uint64_t pack(int mg, int eg) {
        return (static_cast<std::uint64_t>(static_cast<std::uint16_t>(
                    static_cast<std::int16_t>(mg)))) |
               (static_cast<std::uint64_t>(static_cast<std::uint16_t>(
                    static_cast<std::int16_t>(eg)))
                << 16);
    }
    static void unpack(std::uint64_t d, int& mg, int& eg) {
        mg = static_cast<std::int16_t>(d & 0xFFFF);
        eg = static_cast<std::int16_t>((d >> 16) & 0xFFFF);
    }

    PawnBucket table_[kCount];
};

// Program genelinde tek örnek (evaluate() bunu kullanır; global TT gibi erişilir).
extern PawnTable PAWN_TABLE;

}  // namespace engine
