// Statik değerlendirme implementasyonu.

#include "engine/eval.hpp"

#include "engine/bitboard.hpp"
#include "engine/board.hpp"

namespace engine {

int evaluate(const Board& b) {
    int score = 0;  // beyaz bakışıyla

    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        int material = MaterialValue[pt];

        // Beyaz taşlar: +materyal +PST
        Bitboard wbb = b.pieces[pt] & b.colors[WHITE];
        while (wbb) {
            Square sq = pop_lsb(wbb);
            score += material + PST[pt][sq];
        }

        // Siyah taşlar: -materyal -PST (dikey ayna: sq ^ 56)
        Bitboard bbb = b.pieces[pt] & b.colors[BLACK];
        while (bbb) {
            Square sq = pop_lsb(bbb);
            score -= material + PST[pt][static_cast<int>(sq) ^ 56];
        }
    }

    // Hamle sırası olan tarafın bakış açısına çevir.
    return (b.side_to_move == WHITE) ? score : -score;
}

}  // namespace engine
