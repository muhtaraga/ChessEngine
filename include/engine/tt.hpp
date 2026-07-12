#pragma once

// Transposition table (TT): daha önce aranmış pozisyonların sonucunu Zobrist
// anahtarıyla önbelleğe alır. Aynı pozisyona farklı hamle sıralarıyla ("aktarım"
// / transposition) ulaşıldığında arama tekrarını önler ve saklanan en iyi hamle
// move ordering'i güçlendirir.
//
// Bu sınıf bilinçli olarak "aptal"dır: ply'ye bağlı mat-skoru düzeltmesi arama
// tarafında (search.cpp) yapılır; TT yalnızca ham değeri saklar.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

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

// TTEntry: probe()'un AÇILMIŞ (unpacked) çıktı tipi. İç depolama bundan farklı:
// yarış-dayanıklılık için her yuva iki 64-bit atomik söz (key-word + data-word)
// olarak tutulur (bkz. Bucket, aşağıda). Anahtar dışındaki veri TAM 8 bayt:
// score(2)+eval(2)+move(2)+depth(1)+gen_bound(1); Faz 2D lockless-XOR şeması bu
// data sözünü tek uint64'e paketler. Alan eklerken bu 8 baytlık bütçeyi koru.
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

// Lockless-XOR yuvası (Hyatt): iki 64-bit atomik söz.
//   key  = gerçek Zobrist anahtarı XOR paketlenmiş data
//   data = paketlenmiş anahtar-dışı veri (score|eval|move|depth|gen_bound)
// Çok-thread'de bir söz güncel diğeri bayat okunursa (yırtık okuma) (key ^ data)
// gerçek anahtara eşleşmez -> probe miss döner (bozuk giriş kullanılmaz). Tek
// thread'te davranış birebir korunur (paketleme kayıpsız round-trip yapar).
// std::atomic<uint64_t> relaxed: derleyicinin iki sözü yırtmasını/yeniden
// sıralamasını engeller; x86-64'te hizalı 64-bit yük/saklama zaten atomiktir.
struct alignas(16) Bucket {
    std::atomic<std::uint64_t> key{0};
    std::atomic<std::uint64_t> data{0};
};

static_assert(sizeof(Bucket) == 16,
              "Bucket 16 bayt olmalı (cache line'a 4 yuva; TTEntry ile aynı bütçe)");

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

    // Yuva sayısı (0 = tablo yok).
    std::size_t size() const { return count_; }

private:
    static constexpr std::size_t kDefaultSizeMb = 16;

    // Atomik yuvalar kopyalanamaz -> std::vector yerine unique_ptr<Bucket[]>
    // (make_unique dizi biçimi atomikleri sıfır başlatır; clear() döngüyle 0 yazar).
    std::unique_ptr<Bucket[]> table_;
    std::size_t               count_      = 0;  // yuva sayısı (2'nin kuvveti)
    std::size_t               mask_       = 0;  // index maskesi (count-1)
    std::uint8_t              generation_ = 0;
};

// Program genelinde tek TT örneği (arama bu örneği kullanır).
extern TranspositionTable TT;

}  // namespace engine
