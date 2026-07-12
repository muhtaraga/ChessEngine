#pragma once

// Texel tuning: (FEN, oyun sonucu) veri setine karşı eval ağırlıklarını, statik
// eval'in sigmoid'i ile sonuç arasındaki ortalama kare hatayı (MSE) minimize
// edecek şekilde ayarlar.
//
// Yöntem: eval tunable parametrelerde DOĞRUSAL olduğundan (king safety dondurulur,
// katkısı sabit), her pozisyon için taban eval + seyrek özellik (feature) vektörü
// bir kez hesaplanır (finite-difference ile motorun kendi eval_accumulate'inden ->
// sapma yok). Sonra gradyan inişi (Adam) parametreleri günceller. Sigmoid ölçek
// sabiti K önce kalibre edilir.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/board.hpp"
#include "engine/eval.hpp"

namespace engine {

// (pozisyon, beyaz-bakışı oyun sonucu) veri seti.
struct TexelData {
    std::vector<Board>  pos;
    std::vector<double> result;  // 0.0 (siyah kazandı) / 0.5 / 1.0 (beyaz kazandı)
};

// "<FEN> <result>" satırlarından veri seti yükler (datagen çıktısı). Başarılıysa true.
bool load_texel_data(const std::string& path, TexelData& out);

// Sigmoid ölçek sabiti: sigmoid(s) = 1 / (1 + 10^(-K*s/400)), s = beyaz-bakışı eval (cp).
// MSE'yi minimize eden K'yı [lo, hi] aralığında altın-oran benzeri daraltmayla bulur.
// g_eval'in O ANKİ değerlerini kullanır (statik eval üzerinden).
double find_best_k(const TexelData& d, double lo = 0.0, double hi = 4.0, int iters = 40);

// g_eval'in o anki haliyle MSE (verilen K ile). Gerçek evaluate() kullanır.
double texel_mse(const TexelData& d, double k);

struct TuneConfig {
    double k       = 0.0;    // <=0 -> otomatik kalibre
    int    epochs  = 3000;   // gradyan inişi iterasyonu
    double lr      = 2.0;    // Adam öğrenme oranı
    // Varsayılana doğru decoupled weight decay (AdamW): tune edilen parametreleri
    // elle-seçilmiş varsayılana çeker -> aşırı-uyumu ve büyük ölçek kaymalarını
    // önler (ilk tune-tümü geçişi -110 Elo regresyon verdi: eval şişti, dondurulmuş
    // cp arama marjlarını bozdu). 0 -> regularizasyon yok.
    double reg     = 0.0;
    // Materyali dondur: klasik değerlerde (100/320/330/500/900) SABİT tut. Materyal
    // eval magnitüdünü domine ettiğinden bu, eval ölçeğini çıpalar -> dondurulmuş
    // arama marjları geçerli kalır. true -> yalnız PST + pozisyonel tune edilir.
    bool   freeze_material = true;
    bool   verbose = false;  // ilerlemeyi stderr'e bas
};

struct TuneResult {
    double k         = 0.0;
    double mse_start = 0.0;
    double mse_end   = 0.0;
    int    epochs    = 0;
};

// Texel tuning'i çalıştırır: g_eval'in TUNABLE parametrelerini (king safety hariç,
// bkz. eval_frozen_start) günceller. Dönüşte g_eval tune edilmiş değerleri taşır.
TuneResult run_texel_tune(const TexelData& d, const TuneConfig& cfg);

}  // namespace engine
