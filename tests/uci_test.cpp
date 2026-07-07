// UCI protokol testleri: komutları stringstream ile besleyip çıktıyı doğrular.

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/uci.hpp"

using namespace engine;

namespace {
std::string run_uci(const std::string& input) {
    init_sliding_attacks();
    std::istringstream in(input);
    std::ostringstream out;
    uci_loop(in, out);
    return out.str();
}
}  // namespace

TEST(Uci, HandshakeAndReady) {
    std::string out = run_uci("uci\nisready\nquit\n");
    EXPECT_NE(out.find("id name ChessEngine"), std::string::npos);
    EXPECT_NE(out.find("uciok"), std::string::npos);
    EXPECT_NE(out.find("readyok"), std::string::npos);
}

TEST(Uci, PositionAndGoProducesBestmove) {
    std::string out = run_uci(
        "position startpos moves e2e4 e7e5\n"
        "go depth 3\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    // Legal bir hamle üretilmeli (0000 mat/pat değil).
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);
    EXPECT_NE(out.find("info depth 3"), std::string::npos);
}

TEST(Uci, FindsMateViaGo) {
    // Ra8# pozisyonu FEN ile; go depth 2 mat bulmalı.
    std::string out = run_uci(
        "position fen 6k1/5ppp/8/8/8/8/8/R6K w - - 0 1\n"
        "go depth 2\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove a1a8"), std::string::npos);
    EXPECT_NE(out.find("score mate 1"), std::string::npos);
}

TEST(Uci, PromotionMoveParsed) {
    // Beyaz b7-b8 vezir promosyonu legal ve uygulanabilir olmalı.
    std::string out = run_uci(
        "position fen 4k3/1P6/8/8/8/8/8/4K3 w - - 0 1 moves b7b8q\n"
        "go depth 1\n"
        "quit\n");
    // Promosyondan sonra hata olmadan bir bestmove üretilmeli.
    EXPECT_NE(out.find("bestmove "), std::string::npos);
}
