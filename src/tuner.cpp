// Texel tuner implementasyonu. Bkz. tuner.hpp.

#include "engine/tuner.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

#include "engine/bitboard.hpp"

namespace engine {

namespace {

// sigmoid(s) = 1 / (1 + 10^(-K*s/400)), s beyaz-bakışı eval (cp).
inline double sigmoid(double s, double k) {
    return 1.0 / (1.0 + std::pow(10.0, -k * s / 400.0));
}

// Beyaz bakışı statik eval (evaluate() side-to-move döner; beyaza çevir).
inline double eval_white(const Board& b) {
    int e = evaluate(b);
    return (b.side_to_move == WHITE) ? e : -e;
}

// Bir pozisyon için beyaz-bakışı taban eval (float taper) + faz.
inline double base_eval_white_of(const Board& b, int& phase_out) {
    int mg, eg;
    eval_accumulate(b, mg, eg);
    int phase = game_phase(b);
    phase_out = phase;
    return (mg * phase + eg * (MAX_PHASE - phase)) / static_cast<double>(MAX_PHASE);
}

}  // namespace

bool load_texel_data(const std::string& path, TexelData& out) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream is(line);
        std::string f[6], res;
        bool ok = true;
        for (int i = 0; i < 6; ++i)
            if (!(is >> f[i])) { ok = false; break; }
        if (!ok || !(is >> res)) continue;  // bozuk satırı atla
        std::string fen = f[0] + ' ' + f[1] + ' ' + f[2] + ' ' + f[3] + ' ' + f[4] + ' ' + f[5];

        Board b;
        if (!b.set_fen(fen)) continue;
        double r;
        try { r = std::stod(res); } catch (...) { continue; }

        out.pos.push_back(b);
        out.result.push_back(r);
    }
    return !out.pos.empty();
}

double texel_mse(const TexelData& d, double k) {
    double sum = 0.0;
    const std::size_t n = d.pos.size();
    for (std::size_t i = 0; i < n; ++i) {
        double s = sigmoid(eval_white(d.pos[i]), k);
        double e = d.result[i] - s;
        sum += e * e;
    }
    return n ? sum / n : 0.0;
}

double find_best_k(const TexelData& d, double lo, double hi, int iters) {
    // Altın-oran daraltma (MSE(K) tek-modlu varsayılır -> pratikte geçerli).
    const double gr = (std::sqrt(5.0) - 1.0) / 2.0;  // ~0.618
    double c = hi - gr * (hi - lo);
    double e = lo + gr * (hi - lo);
    double fc = texel_mse(d, c);
    double fe = texel_mse(d, e);
    for (int i = 0; i < iters; ++i) {
        if (fc < fe) { hi = e; e = c; fe = fc; c = hi - gr * (hi - lo); fc = texel_mse(d, c); }
        else         { lo = c; c = e; fc = fe; e = lo + gr * (hi - lo); fe = texel_mse(d, e); }
    }
    return (lo + hi) / 2.0;
}

TuneResult run_texel_tune(const TexelData& d, const TuneConfig& cfg) {
    TuneResult res;
    const std::size_t n = d.pos.size();
    const int F = eval_frozen_start();  // tune edilen düz parametre sayısı [0, F)

    // Pawn hash cache'i tuning boyunca KAPAT: finite-difference pawn ağırlıklarını
    // (isolated/doubled/passed) tek tek perturbe edip eval_accumulate çağırıyor;
    // cache açıksa perturbe edilmiş param için BAYAT değer döner -> pawn gradyanları
    // ~0 -> hiç tune edilmez. Tune tek seferlik alt-komut, geri açmaya gerek yok.
    g_pawn_cache_enabled = false;

    std::vector<int*> ptrs = flat_param_pointers(g_eval);

    // w0: tune edilen parametrelerin başlangıç (varsayılan) değerleri.
    std::vector<double> w0(F), w(F);
    for (int i = 0; i < F; ++i) { w0[i] = *ptrs[i]; w[i] = w0[i]; }

    // --- Pozisyon başına: taban eval + seyrek özellik (idx, g) vektörü ---
    // Özellikler motorun kendi eval_accumulate'inden türetilir (sapma yok):
    //  - material + PST: doğrudan (yapı önemsiz; işaret finite-diff ile aynı, testli),
    //  - pawn/mobility/bishop/rook: finite-difference (drift-free).
    struct Feat { int idx; double g; };
    std::vector<double>            base_e(n);
    std::vector<std::vector<Feat>> feats(n);

    const int OTHERS = PIECE_TYPE_NB + 2 * PIECE_TYPE_NB * SQUARE_NB;  // pawn/mob/... başlangıcı (=774)
    const int PST_MG = PIECE_TYPE_NB;                                   // 6
    const int PST_EG = PIECE_TYPE_NB + PIECE_TYPE_NB * SQUARE_NB;       // 390

    for (std::size_t i = 0; i < n; ++i) {
        const Board& b = d.pos[i];
        int mg0, eg0, phase;
        eval_accumulate(b, mg0, eg0);
        phase = game_phase(b);
        base_e[i] = (mg0 * phase + eg0 * (MAX_PHASE - phase)) / static_cast<double>(MAX_PHASE);

        const double cmg = phase / static_cast<double>(MAX_PHASE);              // mg taper katsayısı
        const double ceg = (MAX_PHASE - phase) / static_cast<double>(MAX_PHASE);// eg taper katsayısı
        std::vector<Feat>& fv = feats[i];

        // material + PST (doğrudan): taş başına PST göstergesi, tür başına net sayım.
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
            Bitboard wb = b.pieces[pt] & b.colors[WHITE];
            Bitboard bb = b.pieces[pt] & b.colors[BLACK];
            int net = popcount(wb) - popcount(bb);
            if (net != 0) fv.push_back({pt, static_cast<double>(net)});  // material g = netCount
            Bitboard w2 = wb;
            while (w2) {
                int sq = static_cast<int>(pop_lsb(w2));
                if (cmg != 0.0) fv.push_back({PST_MG + pt * SQUARE_NB + sq, +cmg});
                if (ceg != 0.0) fv.push_back({PST_EG + pt * SQUARE_NB + sq, +ceg});
            }
            Bitboard b2 = bb;
            while (b2) {
                int sq = static_cast<int>(pop_lsb(b2)) ^ 56;  // siyah dikey ayna
                if (cmg != 0.0) fv.push_back({PST_MG + pt * SQUARE_NB + sq, -cmg});
                if (ceg != 0.0) fv.push_back({PST_EG + pt * SQUARE_NB + sq, -ceg});
            }
        }

        // pawn/mobility/bishop/rook (finite-difference, [OTHERS, F)).
        for (int idx = OTHERS; idx < F; ++idx) {
            int  save = *ptrs[idx];
            *ptrs[idx] = save + 1;
            int mg1, eg1;
            eval_accumulate(b, mg1, eg1);
            *ptrs[idx] = save;
            double g = ((mg1 - mg0) * cmg) + ((eg1 - eg0) * ceg);
            if (g != 0.0) fv.push_back({idx, g});
        }
    }

    // --- K kalibrasyonu (taban eval üzerinden, model ile tutarlı) ---
    double k = cfg.k;
    if (k <= 0.0) {
        auto mse_k = [&](double kk) {
            double sum = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                double s = sigmoid(base_e[i], kk);
                double er = d.result[i] - s;
                sum += er * er;
            }
            return n ? sum / n : 0.0;
        };
        const double gr = (std::sqrt(5.0) - 1.0) / 2.0;
        double lo = 0.0, hi = 4.0;
        double c = hi - gr * (hi - lo), e = lo + gr * (hi - lo);
        double fc = mse_k(c), fe = mse_k(e);
        for (int i = 0; i < 40; ++i) {
            if (fc < fe) { hi = e; e = c; fe = fc; c = hi - gr * (hi - lo); fc = mse_k(c); }
            else         { lo = c; c = e; fc = fe; e = lo + gr * (hi - lo); fe = mse_k(e); }
        }
        k = (lo + hi) / 2.0;
    }
    res.k = k;

    // Model MSE (w cinsinden): e_i = base_e + Σ (w[idx]-w0[idx]) * g.
    auto model_mse = [&]() {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double e = base_e[i];
            for (const Feat& f : feats[i]) e += (w[f.idx] - w0[f.idx]) * f.g;
            double s = sigmoid(e, k);
            double er = d.result[i] - s;
            sum += er * er;
        }
        return n ? sum / n : 0.0;
    };
    res.mse_start = model_mse();

    // --- Adam gradyan inişi ---
    std::vector<double> m(F, 0.0), v(F, 0.0), grad(F, 0.0);
    const double b1 = 0.9, b2 = 0.999, eps = 1e-8;
    const double ln10 = std::log(10.0);
    const double gscale = 2.0 * ln10 * k / 400.0 / static_cast<double>(n ? n : 1);

    // İlk tune edilen düz indeks: materyal dondurulduysa [0, PIECE_TYPE_NB) atlanır
    // (klasik materyal = ölçek çıpası); değilse yalnız piyon materyali (indeks 0)
    // sabit tutulur (yine ölçek çıpası).
    const int kTuneLo = cfg.freeze_material ? PIECE_TYPE_NB : 1;

    int ep = 0;
    for (; ep < cfg.epochs; ++ep) {
        std::fill(grad.begin(), grad.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            double e = base_e[i];
            for (const Feat& f : feats[i]) e += (w[f.idx] - w0[f.idx]) * f.g;
            double s = sigmoid(e, k);
            double factor = (s - d.result[i]) * s * (1.0 - s);  // dMSE/de (ölçek gscale'de)
            for (const Feat& f : feats[i]) grad[f.idx] += factor * f.g;
        }
        // Adam güncellemesi + decoupled weight decay (AdamW: varsayılana çek).
        double bc1 = 1.0 - std::pow(b1, ep + 1);
        double bc2 = 1.0 - std::pow(b2, ep + 1);
        for (int idx = kTuneLo; idx < F; ++idx) {
            double g = grad[idx] * gscale;
            m[idx] = b1 * m[idx] + (1 - b1) * g;
            v[idx] = b2 * v[idx] + (1 - b2) * g * g;
            double mh = m[idx] / bc1, vh = v[idx] / bc2;
            w[idx] -= cfg.lr * mh / (std::sqrt(vh) + eps);
            // Decoupled weight decay: varsayılandan (w0) sapmayı orantılı kıs. Adam'ın
            // per-parametre normalizasyonundan BAĞIMSIZ -> reg gücü doğrudan yorumlanır.
            if (cfg.reg > 0.0)
                w[idx] -= cfg.lr * cfg.reg * (w[idx] - w0[idx]);
        }
        if (cfg.verbose && (ep % 200 == 0))
            std::cerr << "  epoch " << ep << "  MSE=" << model_mse() << '\n';
    }
    res.epochs = ep;

    // --- Tune edilmiş parametreleri g_eval'e yaz (yuvarla) ---
    for (int idx = 0; idx < F; ++idx)
        *ptrs[idx] = static_cast<int>(std::lround(w[idx]));

    res.mse_end = model_mse();
    return res;
}

}  // namespace engine
