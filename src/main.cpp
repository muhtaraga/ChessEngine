// Debug demo: boş tahtayı ve başlangıç pozisyonunu UTF-8 sembollerle basar.

#include <iostream>

#include "engine/board.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    // Windows konsolunda Unicode figürinlerin doğru basılması için UTF-8 kod
    // sayfasını (65001) etkinleştir. Sembollerin görünmesi terminal fontuna
    // bağlıdır — Windows Terminal önerilir.
    SetConsoleOutputCP(CP_UTF8);
#endif

    engine::Board board;

    board.clear();
    std::cout << "Bos tahta:\n";
    board.print();

    std::cout << '\n';

    board.set_startpos();
    std::cout << "Baslangic pozisyonu:\n";
    board.print();

    return 0;
}
