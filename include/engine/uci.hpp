#pragma once

// UCI (Universal Chess Interface) protokolü. Motoru Arena/Cutechess gibi
// GUI'lere bağlanabilir kılar.

#include <iosfwd>

namespace engine {

// UCI komut döngüsü. Test edilebilirlik için stream'ler parametredir;
// main() std::cin / std::cout geçirir. "quit" görülünce döner.
void uci_loop(std::istream& in, std::ostream& out);

}  // namespace engine
