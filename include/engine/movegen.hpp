#pragma once

// Move generation: pseudo-legal ve legal hamle üretimi.

#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/types.hpp"

namespace engine {

// Bir karenin 'by' rengi tarafından tehdit edilip edilmediği. Şah kontrolü ve
// rok legalliği için kullanılır.
bool is_square_attacked(const Board& b, Square sq, Color by);

// Aynı sorgu, ama doluluk (occ) dışarıdan verilir. Şah hamlesi legalliğinde
// şahın KENDİSİ occ'tan çıkarılmalıdır — yoksa çek veren ışında geriye kaçış
// (şah kendi gölgesinde durduğu için) yanlışlıkla güvenli görünür.
bool is_square_attacked(const Board& b, Square sq, Color by, Bitboard occ);

// Bir düğümde BİR KEZ hesaplanan legallik bağlamı. Üretim bunun maskeleriyle
// doğrudan legal hamle çıkarır; ayrıca çağıran `checkers`'ı okuyup "çekte miyiz?"
// sorusunu bedavaya cevaplayabilir (arama düğüm başına ayrı bir
// is_square_attacked çağrısı yapmasın diye).
struct MoveGenContext {
    Square   ksq          = SQ_NONE;
    Bitboard checkers     = 0;  // şahımıza saldıran rakip taşlar
    Bitboard check_mask   = 0;  // şah-dışı hamlelerin hedefi bu kümede olmalı
    Bitboard pinned       = 0;  // şahımızı bir rakip ışınına karşı kapatan taşlarımız
    int      num_checkers = 0;
};

// side_to_move için legallik bağlamını kurar (checkers / check_mask / pinned).
MoveGenContext make_context(const Board& b);

// side_to_move için pseudo-legal hamleler (şahın açıkta kalması filtrelenmez;
// rok yalnızca tam legal olduğunda üretilir).
void generate_pseudo(const Board& b, MoveList& list);

// side_to_move için tam legal hamleler. Bağlamı zaten kurmuş olan çağıran
// (ör. arama) onu geçerek ikinci bir make_context maliyetinden kurtulur.
void generate_legal(const Board& b, MoveList& list, const MoveGenContext& ctx);
void generate_legal(const Board& b, MoveList& list);

// side_to_move için tam legal "gürültülü" hamleler: yakalamalar (en passant dahil)
// + TÜM promosyonlar (yakalamayan, düz 8. sıra itmeleri de dahil). Quiescence
// bunları arar; sessiz hamleleri hiç üretmemek qsearch'te boşa harcanan legallik
// testlerini ortadan kaldırır.
//
// SIRA GARANTİSİ: üretilen hamlelerin göreli sırası generate_legal'ınkiyle
// AYNIDIR (aynı pseudo-legal tarama düzeninin süzülmüş hâli). Bu yüzden qsearch'ün
// hamle sıralaması ve dolayısıyla arama davranışı birebir korunur.
//
// ÇEKTEYKEN KULLANMA: çekten kaçışlar gürültülü olmak zorunda değildir ve mat
// tespiti tüm legal hamlelerin yokluğuna bakar. Çekteyken generate_legal kullan.
void generate_noisy(const Board& b, MoveList& list, const MoveGenContext& ctx);
void generate_noisy(const Board& b, MoveList& list);

// Bir düğümde BİR KEZ hesaplanan "çek verme" bağlamı. gives_check bununla, çocuk
// tahtayı KURMADAN, hamle başına sabit sayıda bit işlemiyle karar verir — böylece
// budanan hamleler (futility/LMP) kopya + do_move bedelini hiç ödemez.
struct CheckInfo {
    Square   oksq = SQ_NONE;  // rakip şahın karesi
    // check_squares[pt]: bizim `pt` türünden bir taşımız BU karelerden birine
    // oynarsa rakip şaha doğrudan çek verir. KING girdisi daima 0 (şah çek veremez).
    Bitboard check_squares[PIECE_TYPE_NB] = {};
    // Bizim taşlarımızdan, rakip şah ile kendi slider'ımız arasında TEK taş olanlar:
    // ışından çıkarlarsa keşif (discovered) çeki açarlar.
    Bitboard blockers = 0;
};

// side_to_move için çek-verme bağlamını kurar.
CheckInfo make_check_info(const Board& b);

// `m` (side_to_move'un legal bir hamlesi) rakip şaha çek veriyor mu?
// Kopya-tabanlı karşılığı birebir şudur:
//   Board next = b; next.do_move(m);
//   is_square_attacked(next, next.king_square(next.side_to_move), ~next.side_to_move)
// Diferansiyel test (perft ağacında) bu eşitliği iddia eder.
bool gives_check(const Board& b, Move m, const CheckInfo& ci);

// generate_legal'ın eski, KOPYA-TABANLI referans implementasyonu: her pseudo-legal
// hamleyi tahtaya oynayıp şah güvenliğini test eder. Yavaş ama apaçık doğru.
// Yalnızca diferansiyel testte (yeni pin-aware filtrenin oracle'ı) kullanılır —
// magic bitboard'ları ray-tracing'e karşı doğrulama deseninin aynısı.
void generate_legal_reference(const Board& b, MoveList& list);

}  // namespace engine
