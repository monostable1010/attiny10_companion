CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
TARGET := attiny10

.PHONY: all clean test

all: $(TARGET)

$(TARGET): attiny10.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(TARGET)
	./$(TARGET) --help >/dev/null

clean:
	rm -f $(TARGET)
