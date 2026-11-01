CXX = c++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -O0 -g -Isrc

all: build/consensus-lab

build:
	mkdir -p build

build/consensus-lab: src/main.cpp src/lab.cpp src/lab.hpp src/quepaxa.hpp | build
	$(CXX) $(CXXFLAGS) src/main.cpp src/lab.cpp -o $@

build/lab-tests: tests/tests.cpp src/lab.cpp src/lab.hpp src/quepaxa.hpp | build
	$(CXX) $(CXXFLAGS) tests/tests.cpp src/lab.cpp -o $@

test: build/lab-tests build/consensus-lab
	./build/lab-tests
	./tests/experiments.sh

clean:
	rm -rf build

.PHONY: all test clean
