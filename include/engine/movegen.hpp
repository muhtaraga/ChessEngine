#pragma once

// Move generation: pseudo-legal ve legal hamle üretimi.

#include "engine/board.hpp"
#include "engine/move.hpp"
#include "engine/types.hpp"

namespace engine {

// Bir karenin 'by' rengi tarafından tehdit edilip edilmediği (occ = tahtadaki
// tüm taşlar). Şah kontrolü ve rok legalliği için kullanılır.
bool is_square_attacked(const Board& b, Square sq, Color by);

// side_to_move için pseudo-legal hamleler (şahın açıkta kalması filtrelenmez;
// rok yalnızca tam legal olduğunda üretilir).
void generate_pseudo(const Board& b, MoveList& list);

// side_to_move için tam legal hamleler.
void generate_legal(const Board& b, MoveList& list);

}  // namespace engine
