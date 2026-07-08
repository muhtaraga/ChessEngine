#pragma once

// Transposition table (TT): daha önce aranmış pozisyonların sonucunu Zobrist
// anahtarıyla önbelleğe alır. Aynı pozisyona farklı hamle sıralarıyla ("aktarım"
// / transposition) ulaşıldığında arama tekrarını önler ve saklanan en iyi hamle
// move ordering'i güçlendirir.
//
// Bu sınıf bilinçli olarak "aptal"dır: ply'ye bağlı mat-skoru düzeltmesi arama
// tarafında (search.cpp) yapılır; TT yalnızca ham değeri saklar.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/move.hpp"

namespace engine {

// Saklanan skorun arama penceresine göre türü.
//   EXACT: gerçek değer (alpha < skor < beta olan düğüm)
//   LOWER: alt sınır (fail-high: gerçek değer >= skor)
//   UPPER: üst sınır (fail-low: gerçek değer <= skor)
enum class Bound : std::uint8_t { NONE, EXACT, LOWER, UPPER };

struct TTEntry {
    std::uint64_t key   = 0;            // tam Zobrist anahtarı (çakışma doğrulaması)
    std::int32_t  score = 0;            // ply-düzeltmeli skor (search tarafından)
    Move          move  = Move();       // bu düğümdeki en iyi/geçit hamlesi
    std::int16_t  depth = -1;           // bu skorun arandığı kalan derinlik
    Bound         bound = Bound::NONE;  // skorun sınır türü
    std::uint8_t  gen   = 0;            // yaşlandırma nesli (new_search ile artar)
};

class TranspositionTable {
public:
    // Varsayılan boyutla (16 MB) başlar.
    TranspositionTable() { resize(kDefaultSizeMb); }

    // Tabloyu ~mb megabayta yeniden boyutlandırır (2'nin kuvvetine yuvarlanır).
    // İçeriği sıfırlar.
    void resize(std::size_t mb);

    // Tüm girişleri temizler (ör. ucinewgame).
    void clear();

    // Yeni bir arama başlangıcını işaretler (yaşlandırma nesli artar). Böylece
    // eski aramalardan kalan girişler değiştirme sırasında önceliğini yitirir.
    void new_search() { ++generation_; }

    // key için giriş varsa out'a kopyalar ve true döner. Ham skoru döndürür;
    // çağıran mat düzeltmesini kendisi yapar.
    bool probe(std::uint64_t key, TTEntry& out) const;

    // Girişi saklar (derinlik-tercihli + yaş temelli değiştirme).
    void store(std::uint64_t key, int depth, int score, Bound bound, Move move);

    // Giriş sayısı (0 = tablo yok).
    std::size_t size() const { return table_.size(); }

private:
    static constexpr std::size_t kDefaultSizeMb = 16;

    std::vector<TTEntry> table_;
    std::size_t          mask_       = 0;  // index maskesi (size-1); size 2'nin kuvveti
    std::uint8_t         generation_ = 0;
};

// Program genelinde tek TT örneği (arama bu örneği kullanır).
extern TranspositionTable TT;

}  // namespace engine
