#include "engine/datagen.hpp"

#include <random>
#include <string>
#include <vector>

#include "engine/bitboard.hpp"
#include "engine/board.hpp"
#include "engine/movegen.hpp"
#include "engine/search.hpp"
#include "engine/tt.hpp"

namespace engine {

namespace {

// Kaba yetersiz-materyal (forced draw) tespiti: piyon/kale/vezir yoksa ve toplam
// hafif taş (at+fil) en fazla bir taneyse mat imkânsızdır (KvK, KNvK, KBvK).
// Bilinçli olarak dar tutuldu — yalnız kesin ölü materyal beraberlik sayılsın.
bool insufficient_material(const Board& b) {
    if (b.pieces[PAWN] || b.pieces[ROOK] || b.pieces[QUEEN])
        return false;
    return popcount(b.pieces[KNIGHT] | b.pieces[BISHOP]) <= 1;
}

// Bir hamle "gürültülü" mü (yakalama / en passant / promosyon)? Quiet filtresinde
// kullanılır: taktik olarak sallanan pozisyonlar veri setine girmemeli.
bool is_noisy(const Board& b, Move m) {
    if (m.type() == PROMOTION || m.type() == EN_PASSANT)
        return true;
    return test_bit(b.colors[~b.side_to_move], m.to());  // hedefte rakip taş = yakalama
}

}  // namespace

std::uint64_t generate_training_data(const DatagenConfig& cfg, std::ostream& out) {
    std::mt19937_64 rng(cfg.seed);
    std::uint64_t   total_positions = 0;

    // Global TT'yi baştan temizle: koşu, önceki motor durumundan bağımsız olarak
    // yalnız tohuma bağlı ve tekrar-üretilebilir olsun (aksi halde ısınmış TT
    // arama sonuçlarını, dolayısıyla üretilen veriyi kaydırır).
    TT.clear();

    for (int g = 0; g < cfg.games; ++g) {
        Board b;
        b.set_startpos();

        // Oyun boyunca tüm pozisyon anahtarları (tekrar tespiti + search history).
        // Son eleman daima o anki b.key'dir (invariant).
        std::vector<std::uint64_t> hist;
        hist.push_back(b.key);

        // --- Rastgele açılış (çeşitlilik) ---
        bool bad_opening = false;
        for (int i = 0; i < cfg.opening_plies; ++i) {
            MoveList ml;
            generate_legal(b, ml);
            if (ml.size() == 0) { bad_opening = true; break; }  // erken biten hat
            b.do_move(ml[static_cast<int>(rng() % ml.size())]);
            hist.push_back(b.key);
        }
        if (bad_opening)
            continue;  // açılışta terminale düşen oyunu atla (veri yok)

        // --- Arama fazı ---
        SearchTables            tables;   // move-ordering tabloları oyun boyu yaşar
        std::vector<std::string> fens;    // bu oyunda kaydedilen quiet pozisyonlar
        double                   result = 0.5;  // beyaz bakışı; ply-cap -> beraberlik

        for (int ply = 0; ply < cfg.max_game_ply; ++ply) {
            MoveGenContext ctx = make_context(b);
            MoveList       ml;
            generate_legal(b, ml, ctx);

            // Terminal: hamle yok -> çekte mat, değilse pat.
            if (ml.size() == 0) {
                if (ctx.checkers)
                    result = (b.side_to_move == WHITE) ? 0.0 : 1.0;  // sıradaki taraf mat
                else
                    result = 0.5;  // pat
                break;
            }
            // Beraberlik kuralları: 50-hamle, yetersiz materyal, üçlü tekrar.
            if (b.halfmove_clock >= 100 || insufficient_material(b)) {
                result = 0.5;
                break;
            }
            int reps = 0;
            for (std::uint64_t k : hist)
                if (k == b.key) ++reps;  // hist mevcut pozisyonu bir kez içerir
            if (reps >= 3) {
                result = 0.5;
                break;
            }

            // history: kökten ÖNCEKİ pozisyonlar (kök hariç) -> hist son eleman b.key.
            std::vector<std::uint64_t> search_hist(hist.begin(), hist.end() - 1);
            SearchLimits lim;
            lim.max_depth = cfg.depth;
            TT.new_search();  // UCI "go" gibi: nesli ilerlet (girişler yaşlansın)
            SearchResult sr = search_iterative(b, lim, {}, search_hist, &tables);

            if (sr.best == Move()) {  // güvenlik ağı: hamle bulunamadı
                result = 0.5;
                break;
            }

            // Quiet filtresi: çekte değil + en iyi hamle gürültülü değil + skor mat değil.
            if (ctx.checkers == 0 && !is_noisy(b, sr.best) && !is_mate_score(sr.score))
                fens.push_back(b.to_fen());

            b.do_move(sr.best);
            hist.push_back(b.key);
        }

        // Oyun sonucuyla etiketle ve yaz (beyaz bakışı, tek ondalık).
        const char* label = (result == 0.0) ? "0.0" : (result == 1.0) ? "1.0" : "0.5";
        for (const std::string& f : fens) {
            out << f << ' ' << label << '\n';
            ++total_positions;
        }
    }

    out.flush();
    return total_positions;
}

}  // namespace engine
