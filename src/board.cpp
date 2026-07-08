// Board metotlarının implementasyonu.

#include "engine/board.hpp"

#include <array>
#include <cctype>
#include <iostream>
#include <sstream>

#include "engine/bitboard.hpp"
#include "engine/zobrist.hpp"

namespace engine {

void Board::clear() {
    for (int i = 0; i < PIECE_TYPE_NB; ++i)
        pieces[i] = 0;
    colors[WHITE] = 0;
    colors[BLACK] = 0;
    side_to_move    = WHITE;
    en_passant      = SQ_NONE;
    castling_rights = 0;
    halfmove_clock  = 0;
    fullmove_number = 1;
    key             = 0;
}

std::uint64_t Board::compute_key() const {
    std::uint64_t k = 0;
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
            Bitboard bb = pieces[pt] & colors[c];
            while (bb) {
                Square sq = pop_lsb(bb);
                k ^= ZOBRIST.psq[c][pt][sq];
            }
        }
    }
    k ^= ZOBRIST.castling[castling_rights];
    if (en_passant != SQ_NONE)
        k ^= ZOBRIST.en_passant[file_of(en_passant)];
    if (side_to_move == BLACK)
        k ^= ZOBRIST.side;
    return k;
}

void Board::put_piece(Color c, PieceType pt, Square sq) {
    set_bit(pieces[pt], sq);
    set_bit(colors[c], sq);
    key ^= ZOBRIST.psq[c][pt][sq];  // Zobrist anahtarının taş-kare kısmını bakımla
}

void Board::remove_piece(Color c, PieceType pt, Square sq) {
    clear_bit(pieces[pt], sq);
    clear_bit(colors[c], sq);
    key ^= ZOBRIST.psq[c][pt][sq];  // XOR tersine çevrilebilir: kaldırma da toggle
}

PieceType Board::type_on(Square sq) const {
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        if (test_bit(pieces[pt], sq))
            return static_cast<PieceType>(pt);
    return PIECE_TYPE_NB;  // boş kare (çağıran dolu varsayar)
}

Square Board::king_square(Color c) const {
    return lsb(pieces[KING] & colors[c]);
}

void Board::set_startpos() {
    clear();

    // Piyonlar: beyaz 2. sıra, siyah 7. sıra.
    for (int file = 0; file < 8; ++file) {
        put_piece(WHITE, PAWN, make_square(file, 1));
        put_piece(BLACK, PAWN, make_square(file, 6));
    }

    // Arka sıra dizilimi (a'dan h'ye).
    const PieceType back_rank[8] = {
        ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK
    };
    for (int file = 0; file < 8; ++file) {
        put_piece(WHITE, back_rank[file], make_square(file, 0));
        put_piece(BLACK, back_rank[file], make_square(file, 7));
    }

    side_to_move    = WHITE;
    en_passant      = SQ_NONE;
    castling_rights = WHITE_OO | WHITE_OOO | BLACK_OO | BLACK_OOO;
    halfmove_clock  = 0;
    fullmove_number = 1;
    key             = compute_key();
}

bool Board::piece_at(Square sq, Color& out_color, PieceType& out_type) const {
    if (!test_bit(occupancy(), sq))
        return false;

    out_color = test_bit(colors[WHITE], sq) ? WHITE : BLACK;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        if (test_bit(pieces[pt], sq)) {
            out_type = static_cast<PieceType>(pt);
            return true;
        }
    }
    return false;  // tutarlı bir board'da buraya ulaşılmamalı
}

namespace {

// [renk][tür] için Unicode satranç figürini. Beyaz taşlar dolu (♔...),
// siyah taşlar içi boş (♚...) sembollerdir.
const char* piece_glyph(Color c, PieceType pt) {
    // Not: /utf-8 bayrağı sayesinde düz (narrow) literaller UTF-8 kodlanır.
    // C++20'de u8"..." char8_t üretir ve const char*'a atanmaz; o yüzden düz
    // literal kullanıyoruz.
    static const char* glyphs[COLOR_NB][PIECE_TYPE_NB] = {
        // PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING
        { "♙", "♘", "♗", "♖", "♕", "♔" },  // WHITE
        { "♟", "♞", "♝", "♜", "♛", "♚" }   // BLACK
    };
    return glyphs[c][pt];
}

}  // namespace

std::string Board::to_string() const {
    std::string out;
    // 8. sıradan 1. sıraya doğru bas (beyaz altta görünsün).
    for (int rank = 7; rank >= 0; --rank) {
        out += std::to_string(rank + 1);
        out += ' ';
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            Color c;
            PieceType pt;
            if (piece_at(sq, c, pt))
                out += piece_glyph(c, pt);
            else
                out += "·";  // boş kare: orta nokta (·)
            out += ' ';
        }
        out += '\n';
    }
    out += "  a b c d e f g h\n";
    return out;
}

void Board::print() const {
    std::cout << to_string();
}

namespace {

// Bir kareye "dokunulduğunda" (o kareden çıkıldığında ya da o kareye
// gelindiğinde) geçersiz kalan rok haklarını temizleyen maske. Şah veya kale
// hamlesi ve kale yakalaması bu tek işlemle doğru güncellenir:
//   castling_rights &= mask[from] & mask[to]
constexpr std::array<std::uint8_t, SQUARE_NB> make_castle_mask() {
    std::array<std::uint8_t, SQUARE_NB> m{};
    for (auto& x : m) x = 0xF;
    m[E1] &= ~(WHITE_OO | WHITE_OOO);
    m[H1] &= ~WHITE_OO;
    m[A1] &= ~WHITE_OOO;
    m[E8] &= ~(BLACK_OO | BLACK_OOO);
    m[H8] &= ~BLACK_OO;
    m[A8] &= ~BLACK_OOO;
    return m;
}

constexpr auto CastleMask = make_castle_mask();

}  // namespace

void Board::do_move(Move m) {
    const Square   from = m.from();
    const Square   to   = m.to();
    const MoveType mt   = m.type();
    const Color    us   = side_to_move;
    const Color    them = ~us;
    const PieceType pt  = type_on(from);

    // Zobrist: eski en passant sütun anahtarını kaldır (yeni ep aşağıda eklenir),
    // eski rok haklarını sonda karşılaştırmak için sakla. Taş-kare anahtarları
    // put_piece/remove_piece içinde otomatik bakımlıdır.
    const std::uint8_t old_castling = castling_rights;
    if (en_passant != SQ_NONE)
        key ^= ZOBRIST.en_passant[file_of(en_passant)];

    en_passant = SQ_NONE;   // her hamlede sıfırla; çift itme aşağıda yeniden ayarlar
    ++halfmove_clock;

    // --- Yakalama ---
    if (mt == EN_PASSANT) {
        // Yakalanan piyon, hedefin "arkasında" (kaynakla aynı sırada) durur.
        Square cap_sq = make_square(file_of(to), rank_of(from));
        remove_piece(them, PAWN, cap_sq);
        halfmove_clock = 0;
    } else if (test_bit(colors[them], to)) {
        remove_piece(them, type_on(to), to);
        halfmove_clock = 0;
    }

    // --- Taşı taşı ---
    remove_piece(us, pt, from);
    if (mt == PROMOTION)
        put_piece(us, m.promotion_type(), to);
    else
        put_piece(us, pt, to);

    if (pt == PAWN) {
        halfmove_clock = 0;
        // Çift ileri itme -> en passant hedef karesi (atlanan kare).
        if (rank_of(to) - rank_of(from) == 2 || rank_of(to) - rank_of(from) == -2) {
            en_passant = make_square(file_of(from), (rank_of(from) + rank_of(to)) / 2);
            key ^= ZOBRIST.en_passant[file_of(en_passant)];  // yeni ep sütununu ekle
        }
    }

    // --- Rok: kaleyi de taşı ---
    if (mt == CASTLING) {
        int r = rank_of(to);
        Square rook_from, rook_to;
        if (file_of(to) == 6) {              // kısa rok (şah kanadı, g sütunu)
            rook_from = make_square(7, r);
            rook_to   = make_square(5, r);
        } else {                             // uzun rok (vezir kanadı, c sütunu)
            rook_from = make_square(0, r);
            rook_to   = make_square(3, r);
        }
        remove_piece(us, ROOK, rook_from);
        put_piece(us, ROOK, rook_to);
    }

    // --- Rok haklarını güncelle ---
    castling_rights &= CastleMask[from] & CastleMask[to];
    if (castling_rights != old_castling) {
        key ^= ZOBRIST.castling[old_castling];      // eski hak kombinasyonunu çıkar
        key ^= ZOBRIST.castling[castling_rights];   // yeni kombinasyonu ekle
    }

    if (us == BLACK)
        ++fullmove_number;
    side_to_move = them;
    key ^= ZOBRIST.side;  // sıra değişti: side anahtarını toggle et
}

namespace {

// FEN harfini (renk, tür) ikilisine çevirir. Geçersizse false.
bool char_to_piece(char c, Color& color, PieceType& type) {
    color = std::isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'p': type = PAWN;   return true;
        case 'n': type = KNIGHT; return true;
        case 'b': type = BISHOP; return true;
        case 'r': type = ROOK;   return true;
        case 'q': type = QUEEN;  return true;
        case 'k': type = KING;   return true;
        default:  return false;
    }
}

}  // namespace

bool Board::set_fen(const std::string& fen) {
    clear();

    std::istringstream ss(fen);
    std::string placement, side, castling, ep;
    if (!(ss >> placement >> side >> castling >> ep))
        return false;

    // --- Taş dizilimi: 8. sıradan 1. sıraya, '/' ile ayrılır ---
    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            --rank;
            file = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            file += c - '0';
        } else {
            Color color;
            PieceType type;
            if (!char_to_piece(c, color, type) || file > 7 || rank < 0)
                return false;
            put_piece(color, type, make_square(file, rank));
            ++file;
        }
    }

    // --- Sıra ---
    side_to_move = (side == "b") ? BLACK : WHITE;

    // --- Rok hakları ---
    castling_rights = 0;
    for (char c : castling) {
        switch (c) {
            case 'K': castling_rights |= WHITE_OO;  break;
            case 'Q': castling_rights |= WHITE_OOO; break;
            case 'k': castling_rights |= BLACK_OO;  break;
            case 'q': castling_rights |= BLACK_OOO; break;
            case '-': break;
            default:  break;
        }
    }

    // --- En passant hedef karesi ---
    if (ep != "-" && ep.size() >= 2) {
        int f = ep[0] - 'a';
        int r = ep[1] - '1';
        if (f >= 0 && f < 8 && r >= 0 && r < 8)
            en_passant = make_square(f, r);
    }

    // --- Sayaçlar (opsiyonel) ---
    int hm = 0, fm = 1;
    if (ss >> hm) halfmove_clock = static_cast<std::uint16_t>(hm);
    if (ss >> fm) fullmove_number = static_cast<std::uint16_t>(fm);

    // Tüm taş dizilimi ve durum bilgisi kurulduktan sonra anahtarı hesapla.
    key = compute_key();

    return true;
}

}  // namespace engine
