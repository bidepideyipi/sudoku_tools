CXX ?= c++
CXXFLAGS ?= -std=c++14 -O3 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS ?= -lsqlite3

SRC = src/sudoku_generator.cpp src/puzzle_db_writer.cpp src/main.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = generate_puzzles

.PHONY: all clean run benchmark

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp include/sudoku_generator.h include/puzzle_db_writer.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET) --count 1 --difficulty medium

benchmark: $(TARGET)
	./$(TARGET) --benchmark --count 50 --difficulty expert
