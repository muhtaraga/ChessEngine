// Board metotlarının implementasyonu.

#include "engine/board.hpp"

#include <iostream>

#include "engine/bitboard.hpp"

namespace engine {

void Board::clear() {
    for (int i = 0; i < PIECE_TYPE_NB; ++i)
        pieces[i] = 0;
    colors[WHITE] = 0;
    colors[BLACK] = 0;
    side_to_move    = WHITE;
    en_passant      = SQ_NONE;
    castling_rights = 0;
    halfmove_clock  = 0;
    fullmove_number = 1;
}

void Board::put_piece(Color c, PieceType pt, Square sq) {
    set_bit(pieces[pt], sq);
    set_bit(colors[c], sq);
}

void Board::set_startpos() {
    clear();

    // Piyonlar: beyaz 2. sıra, siyah 7. sıra.
    for (int file = 0; file < 8; ++file) {
        put_piece(WHITE, PAWN, make_square(file, 1));
        put_piece(BLACK, PAWN, make_square(file, 6));
    }

    // Arka sıra dizilimi (a'dan h'ye).
    const PieceType back_rank[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };
    for (int file = 0; file < 8; ++file) {
        put_piece(WHITE, back_rank[file], make_square(file, 0));
        put_piece(BLACK, back_rank[file], make_square(file, 7));
    }

    side_to_move    = WHITE;
    en_passant      = SQ_NONE;
    castling_rights = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO;
    halfmove_clock  = 0;
    fullmove_number = 1;
}

bool Board::piece_at(Square sq, Color& out_color, PieceType& out_type) const {
    if (!test_bit(occupancy(), sq))
        return false;

    out_color = test_bit(colors[WHITE], sq) ? WHITE : BLACK;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        if (test_bit(pieces[pt], sq)) {
            out_type = static_cast<PieceType>(pt);
            return true;
        }
    }
    return false;  // tutarlı bir board'da buraya ulaşılmamalı
}

namespace {

// [renk][tür] için Unicode satranç figürini. Beyaz taşlar dolu (♔...),
// siyah taşlar içi boş (♚...) sembollerdir.
const char* piece_glyph(Color c, PieceType pt) {
    // Not: /utf-8 bayrağı sayesinde düz (narrow) literaller UTF-8 kodlanır.
    // C++20'de u8"..." char8_t üretir ve const char*'a atanmaz; o yüzden düz
    // literal kullanıyoruz.
    static const char* glyphs[COLOR_NB][PIECE_TYPE_NB] = {
        // PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
        { "♙", "♘", "♗", "♖", "♕", "♔" },  // WHITE
        { "♟", "♞", "♝", "♜", "♛", "♚" }   // BLACK
    };
    return glyphs[c][pt];
}

}  // namespace

std::string Board::to_string() const {
    std::string out;
    // 8. sıradan 1. sıraya doğru bas (beyaz altta görünsün).
    for (int rank = 7; rank >= 0; --rank) {
        out += std::to_string(rank + 1);
        out += ' ';
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            Color c;
            PieceType pt;
            if (piece_at(sq, c, pt))
                out += piece_glyph(c, pt);
            else
                out += "·";  // boş kare: orta nokta (·)
            out += ' ';
        }
        out += '\n';
    }
    out += "  a b c d e f g h\n";
    return out;
}

void Board::print() const {
    std::cout << to_string();
}

}  // namespace engine
