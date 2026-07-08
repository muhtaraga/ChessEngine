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

// Pozisyonun oyun fazı [0, MAX_PHASE]: MAX_PHASE = tam kadro (orta oyun ucu),
// 0 = yalnız şah+piyon (oyun sonu ucu).
int game_phase(const Board& b);

// Pawn structure katkısı (izole + çift + geçer piyon), BEYAZ − SİYAH bakışıyla,
// MG ve EG ayrı out-param olarak. evaluate() bunu akümülatörlerine ekler; ayrıca
// terimleri PST gürültüsü olmadan izole test etmek için doğrudan çağrılabilir.
void pawn_structure(const Board& b, int& mg, int& eg);

// Hamle sırası olan tarafın bakışından statik değerlendirme (santipiyon).
int evaluate(const Board& b);

}  // namespace engine
