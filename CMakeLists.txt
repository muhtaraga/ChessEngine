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
    src/pawn_table.cpp
    src/eval.cpp
    src/eval_params.cpp
    src/see.cpp
    src/search.cpp
    src/datagen.cpp
    src/tuner.cpp
    src/uci.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# UCI/debug çalıştırılabiliri.
add_executable(chess src/main.cpp)
target_link_libraries(chess PRIVATE engine)

# Testler. SPRT için sürüm derlemesinde kapatılabilir (BUILD_TESTS=OFF) -> gtest
# FetchContent indirmesi ve test derlemesi atlanır, release binary hızlı çıkar.
option(BUILD_TESTS "GoogleTest tabanlı birim testlerini derle" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
