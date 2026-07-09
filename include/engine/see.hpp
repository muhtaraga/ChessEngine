#pragma once

// SEE (Static Exchange Evaluation): bir karedeki taş alışverişinin materyal
// sonucunu STATİK olarak (arama yapmadan) hesaplar. Her iki taraf o kareye en
// ucuz saldıranıyla vurur ve devam etmek uygunsuzsa durur varsayımıyla, net
// materyal kazancını (santipiyon) verir. Klasik "swap algoritması" (Chess
// Programming Wiki: "SEE - The Swap Algorithm").
//
// Kullanım (Faz 2C Adım 3): quiescence'ta kayıplı yakalamaları (see < 0) elemek.
// İleride delta pruning ve LMR/futility kararlarında da kullanılacak.
//
// SINIRLAR: SEE statiktir — pin, çek, hamle sırası taktikleri gibi konumsal
// incelikleri görmez. Bu yüzden ona dayanan budama SEZGISELDİR (SPRT ile ölçülür).

#include "engine/board.hpp"
#include "engine/move.hpp"

namespace engine {

// `m`'in `to()` karesindeki taş alışverişinin net materyal sonucu (santipiyon),
// HAMLE SIRASI olan tarafın bakışıyla (pozitif = sıradaki taraf için kazanç).
//
// ÖN KOŞUL: `m` bir YAKALAMA olmalı — NORMAL (hedef karede rakip taş) ya da
// EN_PASSANT. Promosyonlar desteklenmez (çağıran atlar); düz (yakalama olmayan)
// hamlelerde sonuç anlamsızdır.
int see(const Board& b, Move m);

}  // namespace engine
