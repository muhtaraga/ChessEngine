// UCI protokol testleri: komutları stringstream ile besleyip çıktıyı doğrular.

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "engine/attacks.hpp"
#include "engine/tt.hpp"
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
    // Iterative deepening: ara derinlikler ve baş varyant yazılmalı.
    EXPECT_NE(out.find("info depth 1"), std::string::npos);
    EXPECT_NE(out.find(" pv "), std::string::npos);
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

// Zaman sınırlı arama: makul sürede bir bestmove dönmeli (donmamalı).
TEST(Uci, MovetimeReturnsBestmove) {
    std::string out = run_uci(
        "position startpos\n"
        "go movetime 300\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);
    // En az derinlik 1 tamamlanmalı.
    EXPECT_NE(out.find("info depth 1"), std::string::npos);
}

// "go infinite" zaman sınırı olmadan arar; "stop" gelince durup bestmove verir
// (analiz modu). Donmadan, geçerli bir hamleyle sonlanmalı.
TEST(Uci, InfiniteStopReturnsBestmove) {
    std::string out = run_uci(
        "position startpos\n"
        "go infinite\n"
        "stop\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);  // en az derinlik 1
}

// "uci" el sıkışmasında ayarlanabilir seçenekler ilan edilmeli (GUI görsün).
TEST(Uci, AdvertisesOptions) {
    std::string out = run_uci("uci\nquit\n");
    EXPECT_NE(out.find("option name Hash type spin"), std::string::npos);
    EXPECT_NE(out.find("option name Clear Hash type button"), std::string::npos);
    // Seçenekler uciok'tan ÖNCE gelmeli (UCI grameri).
    EXPECT_LT(out.find("option name Hash"), out.find("uciok"));
}

// Lazy SMP: "Threads" seçeneği ilan edilmeli (spin, min 1, donanım max).
TEST(Uci, AdvertisesThreadsOption) {
    std::string out = run_uci("uci\nquit\n");
    EXPECT_NE(out.find("option name Threads type spin"), std::string::npos);
    EXPECT_NE(out.find("min 1"), std::string::npos);
    EXPECT_LT(out.find("option name Threads"), out.find("uciok"));
}

// Lazy SMP duman testi: Threads>1 ile çok-thread arama yolu (orchestrator + yardımcı
// thread spawn/join + paylaşılan TT eşzamanlı erişim) alıştırılır. Çok-thread arama
// DETERMİNİSTİK DEĞİLDİR -> düğüm/skor/tam-hamle eşitliği İDDİA EDİLMEZ; yalnız
// (a) mat pozisyonunda mat bulunur, (b) normal pozisyonda legal hamle oynanır, (c)
// çökme yok. Sonda Threads=1'e döndürülür (g_threads global, testler arası sızmasın).
TEST(Uci, ThreadsSmpFindsMateAndPlaysLegal) {
    // (a) Çok-thread mat: Ra8# hâlâ bulunmalı (paylaşılan TT + yardımcılar).
    std::string mate = run_uci(
        "setoption name Threads value 4\n"
        "position fen 6k1/5ppp/8/8/8/8/8/R6K w - - 0 1\n"
        "go depth 4\n"
        "quit\n");
    EXPECT_NE(mate.find("bestmove a1a8"), std::string::npos);
    EXPECT_NE(mate.find("score mate"), std::string::npos);

    // (b) Çok-thread normal pozisyon: legal hamle (0000 değil), çökme yok.
    std::string play = run_uci(
        "setoption name Threads value 4\n"
        "position startpos\n"
        "go depth 6\n"
        "setoption name Threads value 1\n"  // testler arası tek-thread'e geri dön
        "quit\n");
    EXPECT_NE(play.find("bestmove "), std::string::npos);
    EXPECT_EQ(play.find("bestmove 0000"), std::string::npos);
}

// "setoption name Hash value N" TT'yi yeniden boyutlandırmalı ve sonrasında
// motor hâlâ düzgün çalışmalı (fonksiyonel regresyon yok).
TEST(Uci, SetHashResizesTT) {
    run_uci("setoption name Hash value 1\nquit\n");
    std::size_t small = TT.size();
    run_uci("setoption name Hash value 64\nquit\n");
    std::size_t big = TT.size();
    EXPECT_GT(big, small);  // 64 MB > 1 MB -> daha fazla giriş

    // Resize sonrası arama hâlâ geçerli hamle üretiyor mu?
    std::string out = run_uci(
        "setoption name Hash value 32\n"
        "position startpos\n"
        "go depth 3\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);

    TT.resize(16);  // testler arası varsayılana geri dön
}

// "setoption name Clear Hash" (buton) çökmeden işlenmeli; motor oynamaya devam.
TEST(Uci, ClearHashHandled) {
    std::string out = run_uci(
        "position startpos\n"
        "go depth 3\n"
        "setoption name Clear Hash\n"
        "position startpos\n"
        "go depth 3\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);
}

// Bozuk/eksik değerler çökme olmadan sessizce yok sayılmalı.
TEST(Uci, MalformedSetoptionIgnored) {
    std::string out = run_uci(
        "setoption name Hash value abc\n"
        "setoption name Hash\n"
        "setoption name Bilinmeyen value 5\n"
        "setoption\n"
        "position startpos\n"
        "go depth 2\n"
        "quit\n");
    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);
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

// "go nodes N": düğüm bütçesiyle arama. Eşit-düğümlü maçın (NNUE tabanı N3) UCI
// tarafındaki kapısı — cutechess bunu motora bu komutla söyler.
TEST(Uci, GoNodesProducesBestmoveAndRespectsBudget) {
    std::string out = run_uci(
        "position startpos\n"
        "go nodes 30000\n"
        "quit\n");

    EXPECT_NE(out.find("bestmove "), std::string::npos);
    EXPECT_EQ(out.find("bestmove 0000"), std::string::npos);

    // Son info satırındaki düğüm sayısı bütçeye yakın olmalı (kat kat üstü değil).
    std::size_t pos = out.rfind(" nodes ");
    ASSERT_NE(pos, std::string::npos);
    unsigned long long n = std::stoull(out.substr(pos + 7));
    EXPECT_LT(n, 90000ull);
}

// "go nodes" verilmeyince hiçbir şey değişmemeli (davranış-koruyan).
TEST(Uci, GoDepthUnaffectedByNodeLimitSupport) {
    std::string out = run_uci(
        "position startpos\n"
        "go depth 8\n"
        "quit\n");
    EXPECT_NE(out.find("info depth 8"), std::string::npos);
    EXPECT_NE(out.find("bestmove "), std::string::npos);
}
