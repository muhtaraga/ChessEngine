#pragma once

// Datagen: Texel tuning (ve ileride NNUE) için self-play ile quiet-etiketli
// veri seti üretimi. Motor kendi kendine oynar; her oyun rastgele açılış
// plyleriyle başlar (çeşitlilik), sonra iki taraf da sabit derinlikte oynar.
// Sonuç bilindiğinde, oyun boyunca kaydedilen QUIET pozisyonlar (çekte değil,
// en iyi hamle yakalama/promosyon değil, skor mat penceresinde değil) beyaz
// bakışıyla oyun sonucuyla etiketlenip yazılır.
//
// Çıktı formatı: satır başına "<FEN> <result>", result ∈ {0.0, 0.5, 1.0}
// (beyaz bakışı: 1.0 = beyaz kazandı, 0.0 = siyah kazandı, 0.5 = beraberlik).

#include <cstdint>
#include <ostream>

namespace engine {

struct DatagenConfig {
    int           games        = 100;  // üretilecek oyun sayısı
    int           depth        = 8;    // her hamlede sabit arama derinliği
    int           opening_plies = 8;   // rastgele açılış hamlesi (çeşitlilik için)
    int           max_game_ply = 400;  // bu ply'a ulaşan oyun beraberlik sayılır
    std::uint64_t seed         = 1;    // rastgele açılış için tohum (tekrar-üretilebilir)
};

// Yapılandırmaya göre self-play verisi üretir ve `out`'a yazar. Arama davranışını
// değiştirmez (motorun mevcut search_iterative'ini sabit derinlikte sürer). Yazılan
// pozisyon (satır) sayısını döndürür.
std::uint64_t generate_training_data(const DatagenConfig& cfg, std::ostream& out);

}  // namespace engine
