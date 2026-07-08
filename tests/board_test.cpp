// Board temel değişmez (invariant) testleri.
// Bu adımda mantık az; testler kare eşlemesini ve clear/set_startpos
// sonrası tahtanın tutarlılığını doğrular. Perft testleri sonraki adımlarda.

#include <bit>  // std::popcount (C++20)

#include <gtest/gtest.h>

#include "engine/bitboard.hpp"
#include "engine/board.hpp"
#include "engine/types.hpp"

using namespace engine;

TEST(SquareMapping, LerfIndices) {
    EXPECT_EQ(static_cast<int>(A1), 0);
    EXPECT_EQ(static_cast<int>(H1), 7);
    EXPECT_EQ(static_cast<int>(A8), 56);
    EXPECT_EQ(static_cast<int>(H8), 63);

    EXPECT_EQ(file_of(H8), 7);
    EXPECT_EQ(rank_of(H8), 7);
    EXPECT_EQ(file_of(A1), 0);
    EXPECT_EQ(rank_of(A1), 0);

    EXPECT_EQ(make_square(4, 0), E1);
    EXPECT_EQ(make_square(3, 7), D8);
}

TEST(BoardClear, EverythingEmpty) {
    Board b;
    b.set_startpos();  // önce doldur, sonra temizle ki clear gerçekten sıfırlasın
    b.clear();

    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        EXPECT_EQ(b.pieces[pt], 0u);
    EXPECT_EQ(b.colors[WHITE], 0u);
    EXPECT_EQ(b.colors[BLACK], 0u);
    EXPECT_EQ(b.occupancy(), 0u);
    EXPECT_EQ(b.side_to_move, WHITE);
    EXPECT_EQ(b.en_passant, SQ_NONE);
    EXPECT_EQ(b.castling_rights, 0);
    EXPECT_EQ(b.fullmove_number, 1);
}

TEST(BoardStartpos, PieceCounts) {
    Board b;
    b.set_startpos();

    EXPECT_EQ(std::popcount(b.occupancy()), 32);
    EXPECT_EQ(std::popcount(b.colors[WHITE]), 16);
    EXPECT_EQ(std::popcount(b.colors[BLACK]), 16);

    EXPECT_EQ(std::popcount(b.pieces[PAWN]), 16);
    EXPECT_EQ(std::popcount(b.pieces[KNIGHT]), 4);
    EXPECT_EQ(std::popcount(b.pieces[BISHOP]), 4);
    EXPECT_EQ(std::popcount(b.pieces[ROOK]), 4);
    EXPECT_EQ(std::popcount(b.pieces[QUEEN]), 2);
    EXPECT_EQ(std::popcount(b.pieces[KING]), 2);

    EXPECT_EQ(b.side_to_move, WHITE);
    EXPECT_EQ(b.fullmove_number, 1);
    EXPECT_EQ(b.castling_rights,
              WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO);
}

TEST(BoardStartpos, KeySquares) {
    Board b;
    b.set_startpos();

    Color c;
    PieceType pt;

    // Beyaz şah e1'de.
    ASSERT_TRUE(b.piece_at(E1, c, pt));
    EXPECT_EQ(c, WHITE);
    EXPECT_EQ(pt, KING);

    // Siyah vezir d8'de.
    ASSERT_TRUE(b.piece_at(D8, c, pt));
    EXPECT_EQ(c, BLACK);
    EXPECT_EQ(pt, QUEEN);

    // e4 başlangıçta boş.
    EXPECT_FALSE(b.piece_at(E4, c, pt));
}

// to_fen: başlangıç pozisyonu standart FEN'i vermeli.
TEST(BoardFen, StartposString) {
    Board b;
    b.set_startpos();
    EXPECT_EQ(b.to_fen(),
              "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

// Round-trip: set_fen -> to_fen çeşitli pozisyonlarda birebir aynı dizeyi vermeli
// (en passant, kısmi rok hakları, sayaçlar dahil).
TEST(BoardFen, RoundTrip) {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        // 1.e4 sonrası: en passant hedefi e3, siyah sırada.
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
        // Kiwipete: karmaşık rok/taş dizilimi.
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        // Kısmi rok hakları + oyun sonu sayaçları.
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 5 39",
        // Yalnız beyaz kanat rok hakkı.
        "r3k2r/8/8/8/8/8/8/R3K2R w Kq - 3 12",
    };
    for (const char* f : fens) {
        Board b;
        ASSERT_TRUE(b.set_fen(f)) << f;
        EXPECT_EQ(b.to_fen(), f) << "round-trip bozuldu: " << f;
    }
}

// do_move sonrası en passant karesi FEN'de doğru üretilmeli (çift piyon itişi).
TEST(BoardFen, EnPassantAfterDoublePush) {
    Board b;
    b.set_startpos();
    b.do_move(Move::make(E2, E4));  // çift itiş -> ep hedefi e3
    EXPECT_EQ(b.to_fen(),
              "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
}
