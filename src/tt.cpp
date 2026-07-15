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

// 4-yollu victim seçiminde bir nesil eskilik, kaç ply sığlığa eşdeğer sayılır.
// Bayat girişler derin olsalar bile önce atılsın (TT bir önbellek; eski aramanın
// derinliği bugünkü aramaya yaramaz). İlk elle-seçim; Blok 4/16 tuning adayı.
constexpr int kReplaceAgePenalty = 8;

}  // namespace

void TranspositionTable::resize(std::size_t mb) {
    if (mb < 1)
        mb = 1;

    // İstenen bayt bütçesine sığan en büyük 2'nin kuvveti kadar yuva.
    std::size_t bytes   = mb * 1024 * 1024;
    std::size_t entries = bytes / sizeof(Bucket);
    if (entries < 1)
        entries = 1;

    // 2'nin kuvvetine aşağı yuvarla (cluster index'inde ucuz maskeleme için).
    std::size_t pow2 = std::size_t{1} << (std::bit_width(entries) - 1);
    if (pow2 < kClusterSize)
        pow2 = kClusterSize;  // en az bir cluster (küçücük TT)

    // make_unique dizi biçimi Bucket'ları value-initialize eder -> atomikler 0.
    // count_ 2'nin kuvveti ve >= kClusterSize -> cluster sayısı da 2'nin kuvveti.
    table_        = std::make_unique<Bucket[]>(pow2);
    count_        = pow2;
    cluster_mask_ = (pow2 / kClusterSize) - 1;
    generation_   = 0;
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

    // Cluster'ın kClusterSize yuvasını tara; ilk tutarlı-eşleşen giriş hit.
    const std::size_t base = cluster_index(key);
    for (std::size_t i = 0; i < kClusterSize; ++i) {
        const Bucket& b = table_[base + i];
        // Data'yı önce, key'i sonra oku (store da aynı sırada yazar: data sonra key).
        // Tüm yırtık okuma pencereleri (eski data + yeni key, ya da yeni data + eski
        // key) (k ^ d) != key verir -> miss; yalnız iki söz de tutarlıyken hit.
        const std::uint64_t d = b.data.load(std::memory_order_relaxed);
        const std::uint64_t k = b.key.load(std::memory_order_relaxed);
        if ((k ^ d) != key)
            continue;

        TTEntry e;
        unpack_data(d, e);
        if (e.bound() == Bound::NONE)
            continue;  // boş yuva (key==0 tesadüfi eşleşmesi) -> başka yuvaya bak
        e.key = key;
        out   = e;
        return true;
    }
    return false;
}

void TranspositionTable::prefetch(std::uint64_t key) const {
    if (table_)
        // Cluster'ın ilk yuvası = 64 baytlık cache line başı -> tüm 4 yuva çekilir.
        _mm_prefetch(reinterpret_cast<const char*>(&table_[cluster_index(key)]),
                     _MM_HINT_T0);
}

void TranspositionTable::store(std::uint64_t key, int depth, int score,
                              Bound bound, Move move, int eval) {
    if (!table_)
        return;

    if (depth > kMaxTtDepth)
        depth = kMaxTtDepth;  // int8 alan sarmasın

    // Cluster'ı tara: (1) aynı-key yuvası varsa onu güncelle (dublicate üretme),
    // (2) yoksa en değersiz yuvayı (victim) seç. Racy okuma zararsız: değiştirme
    // yalnız bir sezgisel, TT bir önbellek.
    const std::size_t base = cluster_index(key);
    Bucket*  target   = nullptr;   // yazılacak yuva
    TTEntry  target_e;             // hedef yuvanın açılmış mevcut içeriği (rich-field için)
    int      best_value = 0;       // en iyi victim adayının değeri (target!=nullptr iken geçerli)

    for (std::size_t i = 0; i < kClusterSize; ++i) {
        Bucket& b = table_[base + i];
        const std::uint64_t cd = b.data.load(std::memory_order_relaxed);
        const std::uint64_t ck = b.key.load(std::memory_order_relaxed);
        TTEntry e;
        unpack_data(cd, e);
        e.key = ck ^ cd;  // saklanan gerçek anahtarı geri kur

        // (1) Aynı pozisyon: depth-preferred + EXACT muafiyeti (eski tek-yuva kuralı).
        // Aynı nesilden DAHA DERİN aranmış, exact-olmayan girişi koru (qsearch depth 0
        // derin negamax'ı ezmesin) -> o durumda HİÇ yazma (başka yuvaya da yazma,
        // dublicate üretme).
        if (e.bound() != Bound::NONE && e.key == key) {
            if (!(depth >= e.depth || bound == Bound::EXACT))
                return;
            target   = &b;
            target_e = e;
            break;
        }

        // (2) Victim adayı: boş yuva en iyi kurban; yoksa (depth - yaş cezası) en
        // düşük olan. Bayat + sığ tercih edilir; derin+taze giriş doğal korunur.
        const int value = (e.bound() == Bound::NONE)
            ? std::numeric_limits<int>::min()
            : e.depth - kReplaceAgePenalty *
                  static_cast<int>((generation_ - e.generation()) & kGenMask);
        if (target == nullptr || value < best_value) {
            best_value = value;
            target     = &b;
            target_e   = e;
        }
    }

    // Zengin-alan koruması yalnız aynı pozisyonun üstüne yazarken (farklı key'de
    // eski move/eval alakasız).
    Move best_move = move;
    std::int16_t new_eval = static_cast<std::int16_t>(eval);
    if (target_e.bound() != Bound::NONE && target_e.key == key) {
        if (best_move == Move())    best_move = target_e.move;
        if (new_eval == kEvalNone)  new_eval  = target_e.eval;
    }

    const std::uint8_t gen_bound = static_cast<std::uint8_t>(
        (generation_ << 2) | static_cast<std::uint8_t>(bound));
    const std::uint64_t nd = pack_data(static_cast<std::int16_t>(score), new_eval,
                                       best_move, static_cast<std::int8_t>(depth),
                                       gen_bound);

    // Data'yı önce, key'i (key ^ data) sonra yaz. Böyle bir okuyucu (data önce)
    // ya iki güncel sözü ya da eski data + yeni key'i görür; ikincisi XOR ile elenir.
    target->data.store(nd, std::memory_order_relaxed);
    target->key.store(key ^ nd, std::memory_order_relaxed);
}

TranspositionTable TT;

}  // namespace engine
