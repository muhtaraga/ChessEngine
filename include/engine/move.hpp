#pragma once

// Move: bir hamleyi 16-bit içinde kodlar (Stockfish tarzı).
//   bit 0-5   : hedef kare (to)
//   bit 6-11  : kaynak kare (from)
//   bit 12-13 : promosyon taşı (0=at,1=fil,2=kale,3=vezir) — yalnızca PROMOTION'da
//   bit 14-15 : hamle türü (MoveType)

#include <cstdint>
#include <string>

#include "engine/types.hpp"

namespace engine {

enum MoveType : int {
    NORMAL,
    PROMOTION,
    EN_PASSANT,
    CASTLING
};

class Move {
public:
    Move() = default;
    constexpr explicit Move(std::uint16_t data) : data_(data) {}

    // Hamle oluşturur. promo yalnızca type == PROMOTION iken anlamlıdır.
    static constexpr Move make(Square from, Square to, MoveType type = NORMAL,
                               PieceType promo = KNIGHT) {
        return Move(static_cast<std::uint16_t>(
            static_cast<int>(to) |
            (static_cast<int>(from) << 6) |
            ((static_cast<int>(promo) - KNIGHT) << 12) |
            (static_cast<int>(type) << 14)));
    }

    constexpr Square   from() const { return static_cast<Square>((data_ >> 6) & 0x3F); }
    constexpr Square   to()   const { return static_cast<Square>(data_ & 0x3F); }
    constexpr MoveType type() const { return static_cast<MoveType>((data_ >> 14) & 0x3); }

    // Promosyon taşı türü (yalnızca type() == PROMOTION iken anlamlı).
    constexpr PieceType promotion_type() const {
        return static_cast<PieceType>(((data_ >> 12) & 0x3) + KNIGHT);
    }

    constexpr std::uint16_t raw() const { return data_; }
    constexpr bool operator==(const Move& o) const { return data_ == o.data_; }
    constexpr bool operator!=(const Move& o) const { return data_ != o.data_; }

    // UCI gösterimi: "e2e4", promosyonda "e7e8q" (debug/divide için).
    std::string to_uci() const {
        auto sq_str = [](Square s) {
            std::string r;
            r += static_cast<char>('a' + file_of(s));
            r += static_cast<char>('1' + rank_of(s));
            return r;
        };
        std::string s = sq_str(from()) + sq_str(to());
        if (type() == PROMOTION) {
            const char pc[4] = {'n', 'b', 'r', 'q'};
            s += pc[(data_ >> 12) & 0x3];
        }
        return s;
    }

private:
    std::uint16_t data_ = 0;
};

// Basit sabit kapasiteli hamle listesi (bir pozisyonda maks ~218 hamle).
struct MoveList {
    Move moves[256];
    int  count = 0;

    void add(Move m) { moves[count++] = m; }
    int  size() const { return count; }

    const Move* begin() const { return moves; }
    const Move* end()   const { return moves + count; }
    Move operator[](int i) const { return moves[i]; }
};

}  // namespace engine
