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
#include <limits>
#include <vector>

#include "engine/move.hpp"

namespace engine {

// Saklanan skorun arama penceresine göre türü.
//   EXACT: gerçek değer (alpha < skor < beta olan düğüm)
//   LOWER: alt sınır (fail-high: gerçek değer >= skor)
//   UPPER: üst sınır (fail-low: gerçek değer <= skor)
enum class Bound : std::uint8_t { NONE, EXACT, LOWER, UPPER };

// "Statik eval yok" işareti (çekteki düğümler eval hesaplamaz). evaluate()
// simetrik ve sınırlı olduğundan bu değeri asla üretemez -> güvenli sentinel.
constexpr std::int16_t kEvalNone = std::numeric_limits<std::int16_t>::min();

// Yaşlandırma nesli 6 bite sığar (gen_bound'un üst bitleri).
constexpr std::uint8_t kGenMask = 0x3F;

// Depth alanı int8; UCI "go depth 200" gibi uçlarda sarmaması için tavan.
constexpr int kMaxTtDepth = 127;

// Anahtar dışındaki veri TAM 8 bayt: Faz 2D'nin lockless-XOR şeması (key ^ data)
// bu geometriye dayanacak. Alan eklerken bu bütçeyi koru.
struct TTEntry {
    std::uint64_t key       = 0;           // tam Zobrist anahtarı (çakışma doğrulaması)
    std::int16_t  score     = 0;           // ply-düzeltmeli skor (search tarafından)
    std::int16_t  eval      = kEvalNone;   // HAM statik eval (TT skoruyla rafine EDİLMEMİŞ)
    Move          move      = Move();      // bu düğümdeki en iyi/geçit hamlesi
    std::int8_t   depth     = -1;          // bu skorun arandığı kalan derinlik
    std::uint8_t  gen_bound = 0;           // üst 6 bit: nesil, alt 2 bit: Bound

    Bound        bound()      const { return static_cast<Bound>(gen_bound & 0x3); }
    std::uint8_t generation() const { return gen_bound >> 2; }
};

static_assert(sizeof(TTEntry) == 16,
              "TTEntry 16 bayt olmalı (anahtar dışı veri tek uint64'e sığsın)");

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
    // Maske SAYAÇTA: nesil girişte yalnız 6 bit saklanıyor; sayaç 63'ü geçerse
    // hiçbir giriş "taze" görünmez ve değiştirme politikası sessizce çöker.
    void new_search() { generation_ = (generation_ + 1) & kGenMask; }

    // key için giriş varsa out'a kopyalar ve true döner. Ham skoru döndürür;
    // çağıran mat düzeltmesini kendisi yapar.
    bool probe(std::uint64_t key, TTEntry& out) const;

    // Girişi saklar (derinlik-tercihli + yaş temelli değiştirme). eval, düğümün
    // HAM statik eval'i (çekteyken kEvalNone); rafine edilmiş değer verilmemeli.
    void store(std::uint64_t key, int depth, int score, Bound bound, Move move,
               int eval = kEvalNone);

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
