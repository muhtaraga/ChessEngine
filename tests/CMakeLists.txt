# GoogleTest'i FetchContent ile çek ve testleri kur.

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)

# Windows/MSVC: gtest'i ana projeyle aynı CRT'ye bağla.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(board_tests board_test.cpp attacks_test.cpp sliding_test.cpp perft_test.cpp eval_test.cpp search_test.cpp see_test.cpp uci_test.cpp zobrist_test.cpp tt_test.cpp pawn_table_test.cpp datagen_test.cpp tuner_test.cpp)
target_link_libraries(board_tests PRIVATE engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(board_tests)
