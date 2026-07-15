// Transposition table implementasyonu.
//
// İç depolama yarış-dayanıklı Bucket dizisidir (iki 64-bit atomik söz, lockless
// XOR). Public probe()/store() arayüzü değişmedi: probe AÇILMIŞ TTEntry döndürür,
// store açık alanları paketler. Tüm threading endişesi bu dosyada izole.

#include "engine/tt.hpp"

#include <bit>
#include <xmmintrin.h>  // _mm_prefetch (MSVC/x86)

namespace engine {

namespace {

// Anahtar-dışı 8 baytı tek uint64'e paketle/aç. Kayıpsız round-trip: score/eval
// int16, depth int8, move 16-bit ham, gen_bound uint8 -> tek thread'de saklanan
// değerler bit-özdeş (davranış-koruyan).
constexpr std::uint64_t pack_data(std::int16_t score, std::int16_t eval, Move move,
                                  std::int8_t depth, std::uint8_t gen_bound) {
    return static_cast<std::uint64_t>(static_cast<std::uint16_t>(score))
         | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(eval)) << 16)
         | (static_cast<std::uint64_t>(move.raw()) << 32)
         | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(depth)) << 48)
         | (static_cast<std::uint64_t>(gen_bound) << 56);
}

// Paketlenmiş data sözünü TTEntry'nin anahtar-dışı alanlarına aç (key ayrıca set edilir).
void unpack_data(std::uint64_t d, TTEntry& e) {
    e.score     = static_cast<std::int16_t>(static_cast<std::uint16_t>(d & 0xFFFF));
    e.eval      = static_cast<std::int16_t>(static_cast<std::uint16_t>((d >> 16) & 0xFFFF));
    e.move      = Move(static_cast<std::uint16_t>((d >> 32) & 0xFFFF));
    e.depth     = static_cast<std::int8_t>(static_cast<std::uint8_t>((d >> 48) & 0xFF));
    e.gen_bound = static_cast<std::uint8_t>((d >> 56) & 0xFF);
}

}  // namespace

void TranspositionTable::resize(std::size_t mb) {
    if (mb < 1)
        mb = 1;

    // İstenen bayt bütçesine sığan en büyük 2'nin kuvveti kadar yuva.
    std::size_t bytes   = mb * 1024 * 1024;
    std::size_t entries = bytes / sizeof(Bucket);
    if (entries < 1)
        entries = 1;

    // 2'nin kuvvetine aşağı yuvarla (index'te ucuz maskeleme için).
    std::size_t pow2 = std::size_t{1} << (std::bit_width(entries) - 1);

    // make_unique dizi biçimi Bucket'ları value-initialize eder -> atomikler 0.
    table_      = std::make_unique<Bucket[]>(pow2);
    count_      = pow2;
    mask_       = pow2 - 1;
    generation_ = 0;
}

void TranspositionTable::clear() {
    // Tüm yuvaları sıfırla (key=0, data=0 -> bound()==NONE). Atomikler tek tek yazılır.
    for (std::size_t i = 0; i < count_; ++i) {
        table_[i].key.store(0, std::memory_order_relaxed);
        table_[i].data.store(0, std::memory_order_relaxed);
    }
    generation_ = 0;
}

bool TranspositionTable::probe(std::uint64_t key, TTEntry& out) const {
    if (!table_)
        return false;

    const Bucket& b = table_[key & mask_];
    // Data'yı önce, key'i sonra oku (store da aynı sırada yazar: data sonra key).
    // Tüm yırtık okuma pencereleri (eski data + yeni key, ya da yeni data + eski
    // key) (k ^ d) != key verir -> miss; yalnız iki söz de tutarlıyken hit.
    const std::uint64_t d = b.data.load(std::memory_order_relaxed);
    const std::uint64_t k = b.key.load(std::memory_order_relaxed);
    if ((k ^ d) != key)
        return false;

    TTEntry e;
    unpack_data(d, e);
    if (e.bound() == Bound::NONE)
        return false;  // boş yuva (key==0 çakışması) ya da geçersiz
    e.key = key;
    out   = e;
    return true;
}

void TranspositionTable::prefetch(std::uint64_t key) const {
    if (table_)
        _mm_prefetch(reinterpret_cast<const char*>(&table_[key & mask_]),
                     _MM_HINT_T0);
}

void TranspositionTable::store(std::uint64_t key, int depth, int score,
                              Bound bound, Move move, int eval) {
    if (!table_)
        return;

    if (depth > kMaxTtDepth)
        depth = kMaxTtDepth;  // int8 alan sarmasın

    Bucket& bucket = table_[key & mask_];

    // Mevcut girişi aç (değiştirme politikası + zengin-alan koruması için). Racy
    // okuma zararsız: bu yalnız bir değiştirme sezgiselidir, TT bir önbellek.
    const std::uint64_t cur_data = bucket.data.load(std::memory_order_relaxed);
    const std::uint64_t cur_key  = bucket.key.load(std::memory_order_relaxed);
    TTEntry e;
    unpack_data(cur_data, e);
    e.key = cur_key ^ cur_data;  // saklanan gerçek anahtarı geri kur

    // Değiştirme politikası: boş yuvayı, eski nesilden bir girişi, en az bizim
    // kadar sığ aranmışı (depth-preferred), ya da aynı pozisyonun exact değerini
    // üzerine yaz. Korunan tek durum: aynı nesilden DAHA DERİN aranmış, exact
    // olmayan bir giriş. ("Aynı pozisyon daima ezer" kuralı bilinçle yok:
    // qsearch girişleri depth 0, o kural altında depth-N negamax'ı ezebilirdi.)
    const bool replace = e.bound() == Bound::NONE ||
                         e.generation() != generation_ ||
                         depth >= e.depth ||
                         (e.key == key && bound == Bound::EXACT);
    if (!replace)
        return;

    // Aynı pozisyona ait bir girişin üstüne yazarken elimizdekinden daha zengin
    // olan alanları kaybetmeyelim: yeni hamle/eval boşsa eskisini koru.
    Move best_move = move;
    if (best_move == Move() && e.key == key)
        best_move = e.move;

    std::int16_t new_eval = static_cast<std::int16_t>(eval);
    if (new_eval == kEvalNone && e.key == key)
        new_eval = e.eval;

    const std::uint8_t gen_bound = static_cast<std::uint8_t>(
        (generation_ << 2) | static_cast<std::uint8_t>(bound));
    const std::uint64_t nd = pack_data(static_cast<std::int16_t>(score), new_eval,
                                       best_move, static_cast<std::int8_t>(depth),
                                       gen_bound);

    // Data'yı önce, key'i (key ^ data) sonra yaz. Böyle bir okuyucu (data önce)
    // ya iki güncel sözü ya da eski data + yeni key'i görür; ikincisi XOR ile elenir.
    bucket.data.store(nd, std::memory_order_relaxed);
    bucket.key.store(key ^ nd, std::memory_order_relaxed);
}

TranspositionTable TT;

}  // namespace engine
