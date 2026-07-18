// EvalParams düz (flat) parametre haritası + kaydet/yükle.
//
// Tunable eval ağırlıklarının her skaleri için bir işaretçi + isim üretiriz. Bu
// hem parametre dosyası serileştirmesinin hem de tuner'ın (finite-difference /
// gradyan) tek doğruluk kaynağıdır -> ikisi asla birbirinden sapmaz.

#include "engine/eval.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace engine {

std::vector<int*> flat_param_pointers(EvalParams& p) {
    std::vector<int*> v;
    v.reserve(1000);

    // 1) material[6]
    for (int i = 0; i < PIECE_TYPE_NB; ++i) v.push_back(&p.material[i]);
    // 2) pst_mg[6][64]
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        for (int sq = 0; sq < SQUARE_NB; ++sq) v.push_back(&p.pst_mg[pt][sq]);
    // 3) pst_eg[6][64]
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        for (int sq = 0; sq < SQUARE_NB; ++sq) v.push_back(&p.pst_eg[pt][sq]);
    // 4) pawn
    v.push_back(&p.isolated_mg); v.push_back(&p.isolated_eg);
    v.push_back(&p.doubled_mg);  v.push_back(&p.doubled_eg);
    for (int i = 0; i < 8; ++i) v.push_back(&p.passed_mg[i]);
    for (int i = 0; i < 8; ++i) v.push_back(&p.passed_eg[i]);
    // 5) mobility
    for (int i = 0; i < PIECE_TYPE_NB; ++i) v.push_back(&p.mobility_mg[i]);
    for (int i = 0; i < PIECE_TYPE_NB; ++i) v.push_back(&p.mobility_eg[i]);
    // 6) bishop pair + rook file
    v.push_back(&p.bishop_pair_mg); v.push_back(&p.bishop_pair_eg);
    v.push_back(&p.rook_open_mg);   v.push_back(&p.rook_open_eg);
    v.push_back(&p.rook_semi_mg);   v.push_back(&p.rook_semi_eg);
    // 6b) threats / hanging (tune edilir)
    v.push_back(&p.threat_by_pawn_mg);  v.push_back(&p.threat_by_pawn_eg);
    v.push_back(&p.threat_by_minor_mg); v.push_back(&p.threat_by_minor_eg);
    v.push_back(&p.threat_by_rook_mg);  v.push_back(&p.threat_by_rook_eg);
    v.push_back(&p.hanging_mg);          v.push_back(&p.hanging_eg);
    // 6c) outpost (tune edilir)
    v.push_back(&p.outpost_knight_mg); v.push_back(&p.outpost_knight_eg);
    v.push_back(&p.outpost_bishop_mg); v.push_back(&p.outpost_bishop_eg);
    // 6d) geçer piyon şah eskortu (tune edilir; yalnız EG)
    v.push_back(&p.passer_king_escort_eg);
    // 6e) kale kendi geçer piyonunun arkasında (tune edilir; yalnız EG)
    v.push_back(&p.rook_behind_passer_eg);
    // 6f) kötü fil (tune edilir; tapered: mg+eg, taban + blokeli)
    v.push_back(&p.bad_bishop_mg);         v.push_back(&p.bad_bishop_eg);
    v.push_back(&p.bad_bishop_blocked_mg); v.push_back(&p.bad_bishop_blocked_eg);
    // 6g) geri piyon (tune edilir; tapered: mg+eg)
    v.push_back(&p.backward_mg);           v.push_back(&p.backward_eg);
    // --- eval_frozen_start() buraya denk gelir ---
    // 7) king safety (DONDURULMUŞ): shield + attack_weight + safety_table
    v.push_back(&p.shield_missing);
    for (int i = 0; i < PIECE_TYPE_NB; ++i) v.push_back(&p.king_attack_weight[i]);
    for (int i = 0; i < 100; ++i) v.push_back(&p.safety_table[i]);

    return v;
}

const std::vector<std::string>& flat_param_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        auto idx = [](const char* base, int i) {
            std::ostringstream os; os << base << '.' << i; return os.str();
        };
        auto idx2 = [](const char* base, int a, int b) {
            std::ostringstream os; os << base << '.' << a << '.' << b; return os.str();
        };
        for (int i = 0; i < PIECE_TYPE_NB; ++i) n.push_back(idx("material", i));
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            for (int sq = 0; sq < SQUARE_NB; ++sq) n.push_back(idx2("pst_mg", pt, sq));
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
            for (int sq = 0; sq < SQUARE_NB; ++sq) n.push_back(idx2("pst_eg", pt, sq));
        n.push_back("isolated_mg"); n.push_back("isolated_eg");
        n.push_back("doubled_mg");  n.push_back("doubled_eg");
        for (int i = 0; i < 8; ++i) n.push_back(idx("passed_mg", i));
        for (int i = 0; i < 8; ++i) n.push_back(idx("passed_eg", i));
        for (int i = 0; i < PIECE_TYPE_NB; ++i) n.push_back(idx("mobility_mg", i));
        for (int i = 0; i < PIECE_TYPE_NB; ++i) n.push_back(idx("mobility_eg", i));
        n.push_back("bishop_pair_mg"); n.push_back("bishop_pair_eg");
        n.push_back("rook_open_mg");   n.push_back("rook_open_eg");
        n.push_back("rook_semi_mg");   n.push_back("rook_semi_eg");
        n.push_back("threat_by_pawn_mg");  n.push_back("threat_by_pawn_eg");
        n.push_back("threat_by_minor_mg"); n.push_back("threat_by_minor_eg");
        n.push_back("threat_by_rook_mg");  n.push_back("threat_by_rook_eg");
        n.push_back("hanging_mg");          n.push_back("hanging_eg");
        n.push_back("outpost_knight_mg"); n.push_back("outpost_knight_eg");
        n.push_back("outpost_bishop_mg"); n.push_back("outpost_bishop_eg");
        n.push_back("passer_king_escort_eg");
        n.push_back("rook_behind_passer_eg");
        n.push_back("bad_bishop_mg");         n.push_back("bad_bishop_eg");
        n.push_back("bad_bishop_blocked_mg"); n.push_back("bad_bishop_blocked_eg");
        n.push_back("backward_mg");           n.push_back("backward_eg");
        n.push_back("shield_missing");
        for (int i = 0; i < PIECE_TYPE_NB; ++i) n.push_back(idx("king_attack_weight", i));
        for (int i = 0; i < 100; ++i) n.push_back(idx("safety_table", i));
        return n;
    }();
    return names;
}

int eval_frozen_start() {
    // material(6) + pst_mg(384) + pst_eg(384) + pawn(20) + mobility(12) +
    // bishop_pair(2) + rook_file(4) + threats(8) + outpost(4) + passer_king_escort(1) +
    // rook_behind_passer(1) + bad_bishop(4) + backward(2) = 832. King safety bundan sonra.
    return PIECE_TYPE_NB                       // material
         + 2 * PIECE_TYPE_NB * SQUARE_NB       // pst_mg + pst_eg
         + 2 + 2 + 8 + 8                        // pawn
         + 2 * PIECE_TYPE_NB                    // mobility
         + 2 + 4                                // bishop pair + rook file
         + 8                                    // threats / hanging
         + 4                                    // outpost (at/fil)
         + 1                                    // geçer piyon şah eskortu (EG)
         + 1                                    // kale kendi geçer piyonunun arkasında (EG)
         + 4                                    // kötü fil (taban mg/eg + blokeli mg/eg)
         + 2;                                   // geri piyon (mg/eg)
}

bool save_eval_params(const EvalParams& p, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    EvalParams copy = p;  // non-const işaretçiler için (okuma amaçlı)
    std::vector<int*>       ptrs  = flat_param_pointers(copy);
    const std::vector<std::string>& names = flat_param_names();
    for (std::size_t i = 0; i < ptrs.size(); ++i)
        out << names[i] << ' ' << *ptrs[i] << '\n';
    return static_cast<bool>(out);
}

bool load_eval_params(EvalParams& p, const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    // isim -> değer eşlemesi (sıra bağımsız, eksik alan varsayılan kalır).
    std::unordered_map<std::string, int> vals;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream is(line);
        std::string name; int value;
        if (is >> name >> value) vals[name] = value;
    }

    std::vector<int*>       ptrs  = flat_param_pointers(p);
    const std::vector<std::string>& names = flat_param_names();
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        auto it = vals.find(names[i]);
        if (it != vals.end()) *ptrs[i] = it->second;
    }
    return true;
}

}  // namespace engine
