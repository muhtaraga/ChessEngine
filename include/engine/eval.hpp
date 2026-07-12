#pragma once

// Statik değerlendirme (evaluation): materyal + piece-square table, oyun fazına
// göre orta oyun (middlegame) / oyun sonu (endgame) interpolasyonu ile.
// Puan santipiyon (centipawn) cinsinden ve HAMLE SIRASI olan tarafın
// bakış açısından döner (pozitif = sıradaki taraf için iyi).
//
// PST değerleri Michniewski "Simplified Evaluation Function" (Chess
// Programming Wiki) tablolarından. Ham tablolar görsel düzende (8. sıra üstte,
// a-h soldan sağa) yazılmıştır; LERF kare indeksine derleme zamanında çevrilir.
//
// Tapered eval: her taş MG ve EG puanına ayrı katkı verir; nihai puan, kalan
// taşlardan hesaplanan "faz" (phase) ile bu iki uçtan interpolasyondur.
// Şu an MG ve EG tabloları yalnızca ŞAH'ta farklı (orta oyunda kenara/roka,
// oyun sonunda merkeze), materyal her iki fazda eşit. Sonraki adımlarda
// (pawn structure, king safety, mobility) diğer terimler bu çerçeveye eklenecek.

#include <array>

#include "engine/types.hpp"

namespace engine {

struct Board;  // board.hpp'de struct olarak tanımlı — MSVC isim düzenlemesi
               // için class/struct anahtarı eşleşmeli.

// Taş başına materyal değeri (santipiyon). Şah 0 (her iki tarafta da var).
// Materyal şu an fazdan bağımsız (MG = EG); ileride ayrılabilir.
inline constexpr int MaterialValue[PIECE_TYPE_NB] = {
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING
};

// Oyun fazı ağırlıkları: kalan taşların toplam ağırlığı fazı verir. Başlangıç
// dizilimi tam kadro = 24 (orta oyun ucu); taşlar azaldıkça 0'a (oyun sonu ucu)
// iner. Piyon ve şah faza katkı vermez (her oyun sonuna kadar sahnede).
inline constexpr int PhaseWeight[PIECE_TYPE_NB] = {
    0,  // PAWN
    1,  // KNIGHT
    1,  // BISHOP
    2,  // ROOK
    4,  // QUEEN
    0   // KING
};

// Tam kadro faz değeri: 4*at? Hayır — renk başına değil, toplam: 4 at + 4 fil +
// 4 kale*2 + 2 vezir*4 = 4 + 4 + 8 + 8 = 24.
inline constexpr int MAX_PHASE = 24;

// --- Pawn structure ağırlıkları (santipiyon, MG/EG ayrı; tapered) ---
// Değerler standart/güvenli aralıkta; net Elo katkısı SPRT ile doğrulanır,
// tek tek ince ayar sonraki iş. Cezalar negatif, bonuslar pozitif.

// İzole piyon (komşu sütunlarda dost piyon yok) — piyon başına ceza.
inline constexpr int IsolatedPenaltyMg = -12;
inline constexpr int IsolatedPenaltyEg = -15;

// Çift piyon (aynı sütunda birden fazla) — ilk piyondan fazlası başına ceza.
// Oyun sonunda daha ağır (hareketsiz, geçemeyen piyon zayıflığı belirginleşir).
inline constexpr int DoubledPenaltyMg = -10;
inline constexpr int DoubledPenaltyEg = -20;

// Geçer piyon bonusu, piyonun SIRASINA (rank index 0-7) göre; ilerledikçe artar.
// Oyun sonunda belirgin daha büyük — tapered eval'in esas kazandığı yer.
// İndeks: beyaz için rank_of(sq), siyah için 7 - rank_of(sq). Uçlar (0,7) piyon
// için imkânsız, 0 bırakıldı.
inline constexpr int PassedBonusMg[8] = {0, 5, 10, 15, 25, 40, 60, 0};
inline constexpr int PassedBonusEg[8] = {0, 10, 20, 35, 60, 90, 120, 0};

// --- Mobility ağırlıkları (santipiyon, MG/EG ayrı; taş türü başına) ---
// Taşın ulaşabildiği (dost taşla dolu OLMAYAN) kare sayısına doğrusal bonus.
// Piyon ve şah hariç: piyon yapısı ayrı ele alınır, şah "mobilitesi" güvenlikle
// ters düşer. Vezir ağırlığı düşük — erken vezir çıkışını ödüllendirmemek için.
// Kale EG'de daha değerli (açık hatlar oyun sonunda kritik).
// İndeks taş türüdür (PAWN..KING).
inline constexpr int MobilityMg[PIECE_TYPE_NB] = {0, 4, 4, 2, 1, 0};
inline constexpr int MobilityEg[PIECE_TYPE_NB] = {0, 4, 4, 4, 2, 0};

// --- Bishop pair + rook on open/semi-open file ağırlıkları (MG/EG ayrı) ---
// Bishop pair: iki (veya daha fazla) fili olan tarafa bonus; açık oyun sonunda
// (uzun diyagonaller) biraz daha değerli.
inline constexpr int BishopPairMg = 30;
inline constexpr int BishopPairEg = 45;
// Kale açık sütunda (o sütunda hiç piyon yok) / yarı-açık sütunda (yalnız rakip
// piyon var). Açık sütun orta oyunda saldırı için daha değerli.
inline constexpr int RookOpenMg = 25;
inline constexpr int RookOpenEg = 15;
inline constexpr int RookSemiMg = 12;
inline constexpr int RookSemiEg = 8;

// --- King safety ağırlıkları (YALNIZ orta oyun; EG=0 -> taper ile solar) ---
// Oyun sonunda şah merkeze/aktifliğe yönelir (KingCentralizedInEndgame), güvenlik
// önemsizleşir; bu yüzden king_safety yalnız MG'ye katkı verir (eg her zaman 0).
//
// Piyon kalkanı: şahın önündeki (renk yönünde iki sıra) kf-1..kf+1 sütunlarında
// dost piyon yoksa, o sütun için "danger" (tehlike, pozitif santipiyon) eklenir.
inline constexpr int ShieldMissingPenalty = 15;  // eksik kalkan sütunu başına
// Şah kenarda değilse (kf-1 veya kf+1 tahta içindeyse) her sütun sayılır; tahta
// kenarındaki geçersiz sütun eksik sayılmaz (aşağıdaki döngü atlar).

// Şah bölgesi (king ring) saldırıları -> "attack units". Rakip taş türü başına
// ağırlık, o taşın şah halkasında vurduğu kare sayısıyla çarpılır (CPW "King
// Safety" örneği). Küçük ağırlıklar kasıtlı: kare-sayısıyla çarpılıp toplanır.
inline constexpr int KingAttackWeight[PIECE_TYPE_NB] = {
    0,  // PAWN  (piyon baskısı ayrı ele alınmıyor)
    2,  // KNIGHT
    2,  // BISHOP
    3,  // ROOK
    5,  // QUEEN
    0   // KING
};

// SafetyTable[units] -> orta oyun "danger" cezası (santipiyon), doğrusal değil:
// düşük baskıda ~0 (normal pozisyonlar cezalanmaz), baskı arttıkça hızla büyür,
// ~çeyrek vezir eşiğinde 500'de doygunlaşır. CPW "King Safety" standart tablosu.
// İndeks 0..99 (units bu aralığa kırpılır).
inline constexpr int SafetyTable[100] = {
      0,   0,   1,   2,   3,   5,   7,   9,  12,  15,
     18,  22,  26,  30,  35,  39,  44,  50,  56,  62,
     68,  75,  82,  85,  89,  97, 105, 113, 122, 131,
    140, 150, 169, 180, 191, 202, 213, 225, 237, 248,
    260, 272, 283, 295, 307, 319, 330, 342, 354, 366,
    377, 389, 401, 412, 424, 436, 448, 459, 471, 483,
    494, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500
};

namespace detail {

// Ham PST tabloları (orta oyun), görsel düzen: indeks 0 = a8, indeks 63 = h1.
inline constexpr int RawPST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {  0,  0,  0,  0,  0,  0,  0,  0,
      50, 50, 50, 50, 50, 50, 50, 50,
      10, 10, 20, 30, 30, 20, 10, 10,
       5,  5, 10, 25, 25, 10,  5,  5,
       0,  0,  0, 20, 20,  0,  0,  0,
       5, -5,-10,  0,  0,-10, -5,  5,
       5, 10, 10,-20,-20, 10, 10,  5,
       0,  0,  0,  0,  0,  0,  0,  0 },
    // KNIGHT
    { -50,-40,-30,-30,-30,-30,-40,-50,
      -40,-20,  0,  0,  0,  0,-20,-40,
      -30,  0, 10, 15, 15, 10,  0,-30,
      -30,  5, 15, 20, 20, 15,  5,-30,
      -30,  0, 15, 20, 20, 15,  0,-30,
      -30,  5, 10, 15, 15, 10,  5,-30,
      -40,-20,  0,  5,  5,  0,-20,-40,
      -50,-40,-30,-30,-30,-30,-40,-50 },
    // BISHOP
    { -20,-10,-10,-10,-10,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5, 10, 10,  5,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0, 10, 10, 10, 10,  0,-10,
      -10, 10, 10, 10, 10, 10, 10,-10,
      -10,  5,  0,  0,  0,  0,  5,-10,
      -20,-10,-10,-10,-10,-10,-10,-20 },
    // ROOK
    {  0,  0,  0,  0,  0,  0,  0,  0,
       5, 10, 10, 10, 10, 10, 10,  5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
       0,  0,  0,  5,  5,  0,  0,  0 },
    // QUEEN
    { -20,-10,-10, -5, -5,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5,  5,  5,  5,  0,-10,
       -5,  0,  5,  5,  5,  5,  0, -5,
        0,  0,  5,  5,  5,  5,  0, -5,
      -10,  5,  5,  5,  5,  5,  0,-10,
      -10,  0,  5,  0,  0,  0,  0,-10,
      -20,-10,-10, -5, -5,-10,-10,-20 },
    // KING (orta oyun): şah kenarda/rok bölgesinde güvenli, merkez tehlikeli.
    { -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -20,-30,-30,-40,-40,-30,-30,-20,
      -10,-20,-20,-20,-20,-20,-20,-10,
       20, 20,  0,  0,  0,  0, 20, 20,
       20, 30, 10,  0,  0, 10, 30, 20 }
};

// Şah oyun-sonu tablosu (görsel düzen): oyun sonunda şah merkeze/aktif olmalı.
// Michniewski "king end game" tablosu. Diğer taşların EG tablosu MG ile aynı.
inline constexpr int RawKingEg[SQUARE_NB] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// Ham görsel-düzen tabloyu LERF-indeksli (beyaz) tabloya çevirir.
constexpr std::array<int, SQUARE_NB> to_lerf(const int (&raw)[SQUARE_NB]) {
    std::array<int, SQUARE_NB> t{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        int rank = sq >> 3, file = sq & 7;
        int vis  = (7 - rank) * 8 + file;  // LERF -> görsel
        t[sq] = raw[vis];
    }
    return t;
}

// Beyaz için LERF-indeksli PST (orta oyun). Siyah için dikey ayna: [sq ^ 56].
constexpr std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> make_pst_mg() {
    std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> t{};
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        t[pt] = to_lerf(RawPST[pt]);
    return t;
}

// Oyun-sonu PST: ŞAH dışında MG ile aynı; ŞAH oyun-sonu tablosunu kullanır.
constexpr std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> make_pst_eg() {
    auto t = make_pst_mg();
    t[KING] = to_lerf(RawKingEg);
    return t;
}

// --- Pawn structure bitboard maskeleri (derleme zamanı) ---

// Her sütunun (file 0-7) sekiz karesini içeren bitboard.
constexpr std::array<Bitboard, 8> make_file_masks() {
    std::array<Bitboard, 8> t{};
    for (int f = 0; f < 8; ++f) {
        Bitboard bb = 0;
        for (int r = 0; r < 8; ++r)
            bb |= Bitboard{1} << (r * 8 + f);
        t[f] = bb;
    }
    return t;
}

// Bir sütunun komşu sütunları (f-1 ve f+1, tahta içinde). İzole tespiti için:
// piyonun sütununun komşularında dost piyon yoksa piyon izoledir.
constexpr std::array<Bitboard, 8> make_adjacent_file_masks() {
    auto files = make_file_masks();
    std::array<Bitboard, 8> t{};
    for (int f = 0; f < 8; ++f) {
        Bitboard bb = 0;
        if (f > 0) bb |= files[f - 1];
        if (f < 7) bb |= files[f + 1];
        t[f] = bb;
    }
    return t;
}

// Geçer piyon "önündeki alan" maskesi: sq'nun sütunu + komşu sütunlar, yalnızca
// ilerideki sıralar (beyaz için rank > rank_of(sq), siyah için rank <). Bu maske
// altında RAKİP piyon yoksa piyon geçerdir (durduramaz/yakalayamazlar).
constexpr std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> make_passed_masks() {
    std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> t{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        int f = sq & 7, r = sq >> 3;
        Bitboard w = 0, b = 0;
        for (int nf = f - 1; nf <= f + 1; ++nf) {
            if (nf < 0 || nf > 7) continue;
            for (int nr = 0; nr < 8; ++nr) {
                Bitboard bit = Bitboard{1} << (nr * 8 + nf);
                if (nr > r) w |= bit;  // beyaz ilerisi (yukarı)
                if (nr < r) b |= bit;  // siyah ilerisi (aşağı)
            }
        }
        t[WHITE][sq] = w;
        t[BLACK][sq] = b;
    }
    return t;
}

}  // namespace detail

// Beyaz bakışıyla orta oyun / oyun sonu PST. Siyah için dikey ayna: [pt][sq ^ 56].
inline constexpr auto PstMg = detail::make_pst_mg();
inline constexpr auto PstEg = detail::make_pst_eg();

// Pawn structure maskeleri (derleme zamanı, beyaz+siyah).
inline constexpr auto FileMask         = detail::make_file_masks();
inline constexpr auto AdjacentFileMask = detail::make_adjacent_file_masks();
inline constexpr auto PassedMask       = detail::make_passed_masks();

// --- Tunable eval parametreleri (Blok 4: Texel tuning) ---
// Yukarıdaki elle-seçilmiş constexpr sabitler artık yalnız VARSAYILAN kaynağı;
// evaluate() ve terim fonksiyonları çalışma-zamanı MUTABLE `g_eval`'den okur, böylece
// tuner ağırlıkları arama/eval'i yeniden derlemeden değiştirebilir. Varsayılan hâlde
// g_eval bu sabitlerle birebir aynıdır -> davranış-koruyan (exact) refactor.
//
// KAPSAM: king safety alanları (shield/attack/safety_table) struct'a alındı ama ilk
// Texel geçişinde DONDURULUR (doğrusal olmayan SafetyTable ayrı ele alınacak).
// game_phase ağırlıkları (PhaseWeight/MAX_PHASE) yapısal -> tunable değil, constexpr.
// SEE + delta pruning eval'den bağımsız kendi `MaterialValue`'sunu (dondurulmuş)
// kullanmaya devam eder; buradaki `material` yalnız evaluate() içindir.
struct EvalParams {
    int material[PIECE_TYPE_NB];                 // taş materyal değeri (evaluate için)
    int pst_mg[PIECE_TYPE_NB][SQUARE_NB];        // orta oyun PST (LERF, beyaz bakışı)
    int pst_eg[PIECE_TYPE_NB][SQUARE_NB];        // oyun sonu PST (MG'den bağımsız)

    int isolated_mg, isolated_eg;                // izole piyon cezası
    int doubled_mg,  doubled_eg;                 // çift piyon cezası
    int passed_mg[8], passed_eg[8];              // geçer piyon bonusu (rank index)

    int mobility_mg[PIECE_TYPE_NB], mobility_eg[PIECE_TYPE_NB];  // mobility ağırlığı

    int bishop_pair_mg, bishop_pair_eg;          // fil çifti bonusu
    int rook_open_mg,  rook_open_eg;             // kale açık sütun
    int rook_semi_mg,  rook_semi_eg;             // kale yarı-açık sütun

    // King safety (yalnız MG; ilk geçişte dondurulur).
    int shield_missing;                          // eksik kalkan sütunu başına ceza
    int king_attack_weight[PIECE_TYPE_NB];       // şah bölgesi saldırı ağırlığı
    int safety_table[100];                       // attack units -> danger (doğrusal değil)
};

// Elle-seçilmiş varsayılan ağırlıklarla dolu EvalParams üretir (yukarıdaki
// constexpr sabitlerden + PstMg/PstEg tablolarından kopyalar).
EvalParams make_default_eval_params();

// Global, çalışma-zamanı eval parametreleri. Varsayılanla başlar; tuner (chess tune)
// ya da parametre dosyası bunu değiştirebilir. evaluate() + terim fonksiyonları okur.
extern EvalParams g_eval;

// Pozisyonun oyun fazı [0, MAX_PHASE]: MAX_PHASE = tam kadro (orta oyun ucu),
// 0 = yalnız şah+piyon (oyun sonu ucu).
int game_phase(const Board& b);

// Pawn structure katkısı (izole + çift + geçer piyon), BEYAZ − SİYAH bakışıyla,
// MG ve EG ayrı out-param olarak. evaluate() bunu akümülatörlerine ekler; ayrıca
// terimleri PST gürültüsü olmadan izole test etmek için doğrudan çağrılabilir.
void pawn_structure(const Board& b, int& mg, int& eg);

// Mobility katkısı (at/fil/kale/vezir ulaşılabilir kare sayısı), BEYAZ − SİYAH,
// MG/EG ayrı out-param. evaluate() akümülatörlerine ekler; izole test edilebilir.
void mobility(const Board& b, int& mg, int& eg);

// Bishop pair katkısı (iki fili olan tarafa bonus), BEYAZ − SİYAH, MG/EG ayrı.
void bishop_pair(const Board& b, int& mg, int& eg);

// Kale açık/yarı-açık sütun katkısı, BEYAZ − SİYAH, MG/EG ayrı.
void rook_on_file(const Board& b, int& mg, int& eg);

// King safety katkısı (piyon kalkanı + şah bölgesi saldırıları), BEYAZ − SİYAH.
// eg her zaman 0 (yalnız orta oyun terimi); mg negatif = beyaz şahı daha güvensiz.
void king_safety(const Board& b, int& mg, int& eg);

// Hamle sırası olan tarafın bakışından statik değerlendirme (santipiyon).
int evaluate(const Board& b);

}  // namespace engine
