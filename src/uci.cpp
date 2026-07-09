// UCI protokol implementasyonu.

#include "engine/uci.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/tt.hpp"

namespace engine {

namespace {

// --- Asenkron arama durumu ---
// Arama ayrı bir thread'de koşar; ana döngü stdin okumaya devam eder ve "stop"/
// "quit" gelince g_stop'u set eder. Böylece "go infinite" analizinde motor sen
// durdurana kadar düşünür (tek düğüm bütçesiyle sınırlı kalmaz).
std::atomic<bool> g_stop{false};
std::thread       g_search_thread;
// Koşan arama kendiliğinden biter mi? (depth/movetime/timed = evet; "go infinite"
// = hayır). "quit" sınırlı aramayı doğal bitişine bırakır (pipe/batch kullanımında
// tam çıktı), yalnızca sınırsız aramayı zorla durdurur.
std::atomic<bool> g_search_bounded{false};

// Move ordering tabloları: bir OYUN boyunca yaşar (aramalar arası birikim korunur,
// her arama başında yaşlanır), "ucinewgame"de temizlenir. Arama thread'inden okunur;
// her go öncesi stop_search() thread'i join ettiği için yarış yok.
SearchTables g_tables;
// Çıktı akışı (out) iki thread'den yazılabildiği için (arama: info/bestmove;
// ana döngü: readyok vб.) tüm yazımlar bu kilit altında yapılır.
std::mutex        g_io_mtx;

// Çalışan aramayı (varsa) durdurup thread'i bekler. Idempotent.
void stop_search() {
    g_stop.store(true, std::memory_order_relaxed);
    if (g_search_thread.joinable())
        g_search_thread.join();
}

// Iterative deepening derinlik tavanı.
constexpr int kMaxDepth = 63;
// Derinlik/zaman verilmediğinde (çıplak "go" ya da "go infinite") varsayılan
// süre bütçesi (ms). Tek iş parçacığı olduğu için gerçek "infinite"/stop
// desteklenmiyor; bunun yerine sınırlı süre aranıp bestmove verilir.
constexpr std::int64_t kDefaultBudgetMs = 3000;
// Zaman yönetimi güvenlik payı (ms): GUI iletişimi/gecikmesi için ayrılan tampon,
// hesaplanan bütçeden düşülür ki saat aşımı (time forfeit) olmasın.
constexpr std::int64_t kOverheadMs = 30;

// UCI hamle dizesini (ör. "e2e4", "e7e8q") mevcut pozisyondaki legal bir
// hamleyle eşleştirir. Böylece rok/en passant/promosyon bayrakları doğru atanır.
// Eşleşme yoksa geçersiz Move döner.
Move parse_move(const Board& b, const std::string& token) {
    MoveList ml;
    generate_legal(b, ml);
    for (Move m : ml)
        if (m.to_uci() == token)
            return m;
    return Move();
}

// "position" komutunu işler: startpos ya da fen, ardından opsiyonel moves.
// history: tekrar (repetition) tespiti için kökten ÖNCEKİ pozisyonların Zobrist
// anahtarlarıyla doldurulur (her do_move'dan önce terk edilen pozisyonun key'i;
// kök = son board dahil edilmez, search onu kendi ekler).
void handle_position(Board& b, std::vector<std::uint64_t>& history,
                     std::istringstream& ss) {
    std::string token;
    ss >> token;
    history.clear();

    if (token == "startpos") {
        b.set_startpos();
        ss >> token;  // muhtemelen "moves"
    } else if (token == "fen") {
        std::string fen;
        // FEN 6 alandan oluşur; "moves" görene kadar topla.
        while (ss >> token && token != "moves")
            fen += token + ' ';
        b.set_fen(fen);
        // token burada ya "moves" ya da boş.
    }

    if (token == "moves") {
        std::string mv;
        while (ss >> mv) {
            Move m = parse_move(b, mv);
            if (m == Move())  // geçersiz hamle -> dur
                break;
            history.push_back(b.key);  // kökten önceki pozisyonun anahtarı
            b.do_move(m);
        }
    }
}

// "setoption" komutunu işler: "setoption name <id> [value <x>]".
// <id> boşluk içerebildiği için ("Clear Hash") token'lar "value" görene kadar
// toplanır (handle_position'daki FEN toplama deseninin aynısı).
void handle_setoption(std::istringstream& ss) {
    std::string token;
    ss >> token;
    if (token != "name")
        return;  // gramer dışı: sessizce yok say

    // Option adını "value" görene (ya da satır bitene) kadar topla.
    std::string name;
    while (ss >> token && token != "value") {
        if (!name.empty()) name += ' ';
        name += token;
    }
    // token == "value" ise geri kalanı değer olarak topla (boşluklu olabilir).
    std::string value;
    if (token == "value") {
        while (ss >> token) {
            if (!value.empty()) value += ' ';
            value += token;
        }
    }

    if (name == "Hash") {
        // TT boyutu (MB). Geçersiz sayı -> yok say. [1, 1024] MB'ye clamp.
        std::int64_t mb = 0;
        try {
            mb = std::stoll(value);
        } catch (...) {
            return;  // bozuk/eksik value: yok say
        }
        if (mb < 1)    mb = 1;
        if (mb > 1024) mb = 1024;
        // resize tabloyu yeniden ayırır; arama thread'i canlıyken koşmamalı.
        stop_search();
        TT.resize(static_cast<std::size_t>(mb));
    } else if (name == "Clear Hash") {
        // Buton: TT'yi elle temizle (analiz için kullanışlı).
        stop_search();
        TT.clear();
    }
    // Tanınmayan option: sessizce yok say (UCI önerisi).
}

// Puanı UCI formatında yazar: "cp X" veya "mate M".
std::string score_string(int score) {
    std::ostringstream os;
    if (is_mate_score(score)) {
        int mate_ply   = MATE - std::abs(score);
        int mate_moves = (mate_ply + 1) / 2;
        os << "mate " << (score > 0 ? mate_moves : -mate_moves);
    } else {
        os << "cp " << score;
    }
    return os.str();
}

// "go" komutunu işler: limitleri belirle, iterative deepening ile ara,
// her derinlikte info yaz, sonunda bestmove ver. history tekrar tespiti için
// search'e (kopyalanarak) verilir.
void handle_go(const Board& b, const std::vector<std::uint64_t>& history,
               std::istringstream& ss, std::ostream& out) {
    int          depth     = -1;
    std::int64_t movetime  = -1;
    std::int64_t wtime = -1, btime = -1, winc = 0, binc = 0;
    std::int64_t movestogo = 0;   // 0 -> bilinmiyor (sudden death / artışlı)
    bool         infinite  = false;

    // Bu arama için TT nesli ilerlet: önceki hamlelerden kalan girişler
    // değiştirmede önceliğini yitirsin (yaşlandırma).
    TT.new_search();

    std::string token;
    while (ss >> token) {
        if      (token == "depth")     ss >> depth;
        else if (token == "movetime")  ss >> movetime;
        else if (token == "wtime")     ss >> wtime;
        else if (token == "btime")     ss >> btime;
        else if (token == "winc")      ss >> winc;
        else if (token == "binc")      ss >> binc;
        else if (token == "movestogo") ss >> movestogo;
        else if (token == "infinite")  infinite = true;
    }

    // Derinlik ve zaman sınırlarını belirle.
    SearchLimits lim;
    lim.max_depth = kMaxDepth;
    lim.stop      = &g_stop;  // "stop"/"quit" ile asenkron kesme

    if (depth > 0) {
        // Sabit derinlik: zaman sınırı yok.
        lim.max_depth = (depth < kMaxDepth) ? depth : kMaxDepth;
    } else if (infinite) {
        // "go infinite": zaman sınırı yok; yalnızca "stop" (veya derinlik tavanı)
        // durdurur. Analiz için: sen durdurana kadar düşünmeye devam eder.
        lim.soft_ms = -1;
        lim.hard_ms = -1;
    } else if (movetime > 0) {
        // Bu hamleye tam olarak bu süre: overhead düşülüp hepsi kullanılır.
        std::int64_t use = movetime - kOverheadMs;
        if (use < 1) use = 1;
        lim.soft_ms = use;
        lim.hard_ms = use;
    } else if (wtime > 0 || btime > 0) {
        std::int64_t t   = (b.side_to_move == WHITE) ? wtime : btime;
        std::int64_t inc = (b.side_to_move == WHITE) ? winc : binc;
        if (t < 0) t = 0;

        // Bu hamleye ayrılacak süreyi asla aşamayacağımız üst sınır.
        std::int64_t max_use = t - kOverheadMs;
        if (max_use < 1) max_use = 1;

        // Hedef pay: movestogo biliniyorsa kalan süreyi hamlelere böl (+1 güvenlik
        // payı), yoksa ~1/30. Artışın yarısını ekle.
        std::int64_t alloc = (movestogo > 0)
                                 ? t / (movestogo + 1) + inc / 2
                                 : t / 30 + inc / 2;
        if (alloc > max_use) alloc = max_use;
        if (alloc < 1)       alloc = 1;

        // soft: hedef (yeni derinliğe başlama eşiği). hard: gerekirse bu derinliği
        // bitirmek için taşabileceğimiz mutlak tavan (payı 3× ama max_use'u aşmaz).
        lim.soft_ms = alloc;
        lim.hard_ms = std::min<std::int64_t>(alloc * 3, max_use);
        if (lim.hard_ms < lim.soft_ms) lim.hard_ms = lim.soft_ms;
    } else {
        // Çıplak "go" (parametresiz): terminal kolaylığı için varsayılan bütçe.
        lim.soft_ms = kDefaultBudgetMs;
        lim.hard_ms = kDefaultBudgetMs;
    }

    // Önceki arama hâlâ koşuyorsa (GUI normalde önce "stop" gönderir) durdur.
    stop_search();
    g_stop.store(false, std::memory_order_relaxed);
    // Sınırlı mı? (zaman sınırı var veya derinlik tavanın altında). "quit"in
    // davranışını belirler.
    g_search_bounded.store(lim.hard_ms >= 0 || lim.max_depth < kMaxDepth,
                           std::memory_order_relaxed);

    // Aramayı ayrı thread'de başlat: ana döngü stdin okumaya devam etsin ki
    // "stop" gelince kesebilelim. Pozisyonun bir KOPYASINI thread'e veriyoruz
    // (ana döngüdeki board sonraki "position" ile değişebilir).
    Board pos = b;
    auto hist = history;  // thread'e taşınacak kopya (ana geçmiş değişebilir)
    g_search_thread = std::thread([pos, lim, hist, &out] {
        using clock = std::chrono::steady_clock;
        auto start = clock::now();
        auto elapsed_ms = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       clock::now() - start).count();
        };

        SearchResult best = search_iterative(pos, lim, [&](const SearchResult& r, int d) {
            std::lock_guard<std::mutex> lk(g_io_mtx);
            auto el = elapsed_ms();
            out << "info depth " << d
                << " score " << score_string(r.score)
                << " nodes " << r.nodes
                << " time "  << el
                << " nps "   << (el > 0 ? r.nodes * 1000 / el : 0);
            if (!r.pv.empty()) {
                out << " pv";
                for (Move m : r.pv)
                    out << ' ' << m.to_uci();
            }
            out << '\n';
            out.flush();
        }, hist, &g_tables);

        std::lock_guard<std::mutex> lk(g_io_mtx);
        if (best.best == Move())
            out << "bestmove 0000\n";  // hamle yok (mat/pat)
        else
            out << "bestmove " << best.best.to_uci() << '\n';
        out.flush();
    });
}

}  // namespace

void uci_loop(std::istream& in, std::ostream& out) {
    Board board;
    board.set_startpos();
    // Oyun pozisyon geçmişi (Zobrist anahtarları): tekrar tespiti için her
    // "position" komutunda yeniden kurulur, "go"da search'e verilir.
    std::vector<std::uint64_t> history;

    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "uci") {
            std::lock_guard<std::mutex> lk(g_io_mtx);
            out << "id name ChessEngine\n";
            out << "id author Muhtar Agcabay\n";
            // GUI'den ayarlanabilir seçenekler (kanonik UCI isimleri; GUI'ler
            // "Hash" ve "Clear Hash"i özel arayüzle tanır).
            out << "option name Hash type spin default 16 min 1 max 1024\n";
            out << "option name Clear Hash type button\n";
            out << "uciok\n";
            out.flush();
        } else if (cmd == "isready") {
            // Arama sürerken bile yanıtlanabilmeli (UCI eşzamanlılık kuralı).
            std::lock_guard<std::mutex> lk(g_io_mtx);
            out << "readyok\n";
            out.flush();
        } else if (cmd == "stop") {
            stop_search();  // arama thread'i o ana kadarki en iyi hamleyi basar
        } else if (cmd == "ucinewgame") {
            stop_search();  // TT'yi temizlemeden önce aramayı durdur (yarış önleme)
            board.set_startpos();
            history.clear();  // yeni oyun: pozisyon geçmişini sıfırla
            TT.clear();  // yeni oyun: önceki oyunun girişlerini at
            g_tables.clear();  // yeni oyun: killer/history/continuation history sıfır
        } else if (cmd == "setoption") {
            handle_setoption(ss);  // Hash / Clear Hash (kendi içinde stop_search)
        } else if (cmd == "position") {
            stop_search();  // eski pozisyon için koşan aramayı bırak
            handle_position(board, history, ss);
        } else if (cmd == "go") {
            handle_go(board, history, ss, out);
        } else if (cmd == "quit") {
            // Sınırsız arama (go infinite) hariç, koşan aramayı doğal bitişine
            // bırak (pipe/batch: tam çıktı). Sonra thread'i bekle ve çık.
            if (!g_search_bounded.load(std::memory_order_relaxed))
                g_stop.store(true, std::memory_order_relaxed);
            if (g_search_thread.joinable())
                g_search_thread.join();
            break;
        }
        // Bilinmeyen komutlar sessizce yok sayılır (UCI önerisi).
    }

    stop_search();  // EOF/döngü sonu: kalan aramayı temizle
}

}  // namespace engine
