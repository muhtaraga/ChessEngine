#pragma once

// Perft (performance test): verilen derinliğe kadar tüm legal hamle
// dizilerinin (yaprak düğüm) sayısını üretir. Move generation'ın doğruluk
// testidir — bilinen referans değerleriyle karşılaştırılır.

#include <cstdint>

#include "engine/board.hpp"
#include "engine/movegen.hpp"

namespace engine {

inline std::uint64_t perft(const Board& b, int depth) {
    if (depth == 0)
        return 1;

    MoveList list;
    generate_legal(b, list);

    // depth == 1'de yaprak sayısı doğrudan legal hamle sayısıdır.
    if (depth == 1)
        return static_cast<std::uint64_t>(list.size());

    std::uint64_t nodes = 0;
    for (Move m : list) {
        Board next = b;
        next.do_move(m);
        nodes += perft(next, depth - 1);
    }
    return nodes;
}

}  // namespace engine
