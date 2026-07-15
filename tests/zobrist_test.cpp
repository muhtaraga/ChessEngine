// Zobrist hashing testleri.
//
// En önemli invariant: do_move'un artımlı anahtar güncellemesi, anahtarı
// sıfırdan hesaplayan compute_key() ile daima uyuşmalı. Bunu birçok standart
// pozisyonda perft benzeri (tüm legal hamle ağacı) bir yürüyüşle doğruluyoruz;
// böylece yakalama, en passant, rok, promosyon, çift itme ve rok-hakkı kaybı
// dahil her hamle türü kapsanır.

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/zobrist.hpp"

using namespace engine;

namespace {

const char* kStartpos =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
const char* kKiwipete =
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
const char* kPosition3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
const char* kPosition4 =
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
const char* kPosition5 =
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";

// Tüm legal hamle ağacını 'depth' derinliğine kadar dolaşır ve HER düğümde
// artımlı anahtarın (b.key) sıfırdan hesapla ile uyuştuğunu iddia eder.
// Uyumsuzlukta test başarısız olur ve sayacı artırır.
void walk_and_check(const Board& b, int depth, int& failures) {
    if (b.key != b.compute_key())
        ++failures;
    // Artımlı pawn_key de sıfırdan hesapla ile uyuşmalı (put/remove_piece bakımı):
    // capture/ep/promosyon/rok/çift-itiş yolları bu ağaçta kapsanır.
    if (b.pawn_key != b.compute_pawn_key())
        ++failures;

    if (depth == 0)
        return;

    MoveList ml;
    generate_legal(b, ml);
    for (Move m : ml) {
        Board next = b;
        next.do_move(m);
        walk_and_check(next, depth - 1, failures);
    }
}

int count_key_mismatches(const std::string& fen, int depth) {
    Board b;
    EXPECT_TRUE(b.set_fen(fen));
    int failures = 0;
    walk_and_check(b, depth, failures);
    return failures;
}

}  // namespace

// --- Artımlı anahtar == sıfırdan hesap (tüm hamle türleri) ---
TEST(Zobrist, IncrementalMatchesRecomputeStartpos) {
    EXPECT_EQ(count_key_mismatches(kStartpos, 4), 0);
}
TEST(Zobrist, IncrementalMatchesRecomputeKiwipete) {
    // Rok, en passant ve yoğun yakalama içerir.
    EXPECT_EQ(count_key_mismatches(kKiwipete, 3), 0);
}
TEST(Zobrist, IncrementalMatchesRecomputePosition3) {
    // En passant ve piyon itişleri.
    EXPECT_EQ(count_key_mismatches(kPosition3, 4), 0);
}
TEST(Zobrist, IncrementalMatchesRecomputePosition4) {
    // Promosyon + rok.
    EXPECT_EQ(count_key_mismatches(kPosition4, 3), 0);
}
TEST(Zobrist, IncrementalMatchesRecomputePosition5) {
    EXPECT_EQ(count_key_mismatches(kPosition5, 3), 0);
}

// --- set_fen sonrası anahtar tutarlı ---
TEST(Zobrist, SetFenInitializesKey) {
    Board b;
    ASSERT_TRUE(b.set_fen(kKiwipete));
    EXPECT_EQ(b.key, b.compute_key());
    EXPECT_NE(b.key, 0u);  // dolu bir pozisyonun anahtarı sıfır olmamalı
}

TEST(Zobrist, StartposInitializesKey) {
    Board b;
    b.set_startpos();
    EXPECT_EQ(b.key, b.compute_key());
}

// --- Transposition: farklı hamle sıralaması aynı pozisyona -> aynı anahtar ---
TEST(Zobrist, TranspositionSameKey) {
    // 1.Nf3 Nf6 2.Ng1 Ng8 başlangıç pozisyonuna döner (ep hakları da aynı).
    Board a;
    a.set_startpos();
    const std::uint64_t start_key = a.key;

    auto play = [](Board& b, const char* uci) {
        MoveList ml;
        generate_legal(b, ml);
        for (Move m : ml)
            if (m.to_uci() == uci) { b.do_move(m); return true; }
        return false;
    };

    ASSERT_TRUE(play(a, "g1f3"));
    ASSERT_TRUE(play(a, "g8f6"));
    ASSERT_TRUE(play(a, "f3g1"));
    ASSERT_TRUE(play(a, "f6g8"));

    EXPECT_EQ(a.key, start_key);          // aynı pozisyon, aynı anahtar
    EXPECT_EQ(a.key, a.compute_key());
}

// --- Sıra farkı anahtarı değiştirir (aynı taş dizilimi, farklı side) ---
TEST(Zobrist, SideToMoveAffectsKey) {
    Board w, bl;
    ASSERT_TRUE(w.set_fen("8/8/8/4k3/8/4K3/8/8 w - - 0 1"));
    ASSERT_TRUE(bl.set_fen("8/8/8/4k3/8/4K3/8/8 b - - 0 1"));
    EXPECT_NE(w.key, bl.key);
    // Tek fark side anahtarı olduğundan, XOR'ları tam olarak ZOBRIST.side olmalı.
    EXPECT_EQ(w.key ^ bl.key, ZOBRIST.side);
}

// --- Rok hakları anahtarı değiştirir ---
TEST(Zobrist, CastlingRightsAffectKey) {
    Board full, none;
    ASSERT_TRUE(full.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    ASSERT_TRUE(none.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1"));
    EXPECT_NE(full.key, none.key);
}

// --- En passant hedefi anahtarı değiştirir ---
TEST(Zobrist, EnPassantAffectsKey) {
    Board with_ep, without_ep;
    ASSERT_TRUE(with_ep.set_fen(
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"));
    ASSERT_TRUE(without_ep.set_fen(
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3"));
    EXPECT_NE(with_ep.key, without_ep.key);
}
