CXX ?= c++
CXXFLAGS ?= -std=c++14 -O3 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?= -lsqlite3

COMMON_GEN = src/sudoku_generator.cpp
FREE_SRC = $(COMMON_GEN) src/puzzle_db_writer.cpp src/main.cpp
DUNGEON_SRC = $(COMMON_GEN) src/mini_sudoku_generator.cpp src/dungeon_db_writer.cpp src/main_dungeon.cpp

FREE_OBJ = $(FREE_SRC:.cpp=.o)
DUNGEON_OBJ = $(DUNGEON_SRC:.cpp=.o)

FREE_TARGET = generate_puzzles
DUNGEON_TARGET = generate_dungeon_puzzles

.PHONY: all clean run benchmark run-dungeon

all: $(FREE_TARGET) $(DUNGEON_TARGET)

$(FREE_TARGET): $(FREE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(DUNGEON_TARGET): $(DUNGEON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(FREE_OBJ) $(DUNGEON_OBJ) $(FREE_TARGET) $(DUNGEON_TARGET)

run: $(FREE_TARGET)
	./$(FREE_TARGET) --count 1 --difficulty medium

benchmark: $(FREE_TARGET)
	./$(FREE_TARGET) --benchmark --count 50 --difficulty expert

run-dungeon: $(DUNGEON_TARGET)
	./$(DUNGEON_TARGET) --verbose --per-pool 10 --out ../db/puzzles.db
