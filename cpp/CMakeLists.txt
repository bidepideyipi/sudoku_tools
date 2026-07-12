cmake_minimum_required(VERSION 3.10)
project(sudoku_generator CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(generate_puzzles
  src/sudoku_generator.cpp
  src/puzzle_db_writer.cpp
  src/main.cpp
)
target_include_directories(generate_puzzles PRIVATE include)
target_link_libraries(generate_puzzles PRIVATE sqlite3)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(generate_puzzles PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
