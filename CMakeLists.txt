cmake_minimum_required(VERSION 3.20)

project(ChessEngine LANGUAGES CXX)

# C++20 zorunlu.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: UTF-8 kaynak/çalıştırma karakter setini garanti et (u8"..." figürinler
# ve Türkçe yorumlar için).
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Çekirdek motor kütüphanesi. Move generation, search vb. buraya eklenecek.
add_library(engine STATIC
    src/board.cpp
    src/attacks.cpp
    src/movegen.cpp
    src/zobrist.cpp
    src/tt.cpp
    src/eval.cpp
    src/search.cpp
    src/uci.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# UCI/debug çalıştırılabiliri.
add_executable(chess src/main.cpp)
target_link_libraries(chess PRIVATE engine)

# Testler.
enable_testing()
add_subdirectory(tests)
