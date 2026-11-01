CXX = c++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -O0 -g -Isrc

CORE_SOURCES = src/config.cpp src/client.cpp src/application.cpp src/reads.cpp \
               src/network.cpp src/runtime.cpp src/protocol_factory.cpp \
               src/protocols/quepaxa_driver.cpp src/protocols/async_replication.cpp \
               src/inspection.cpp src/checks.cpp
HEADERS = $(wildcard src/*.hpp src/protocols/*.hpp)

all: build/consensus-lab

build:
	mkdir -p build

build/consensus-lab: src/main.cpp $(CORE_SOURCES) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) src/main.cpp $(CORE_SOURCES) -o $@

build/lab-tests: tests/tests.cpp tests/protocol_boundary.cpp $(CORE_SOURCES) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) tests/tests.cpp tests/protocol_boundary.cpp $(CORE_SOURCES) -o $@

test: build/lab-tests build/consensus-lab
	./build/lab-tests
	./tests/experiments.sh

clean:
	rm -rf build

.PHONY: all test clean
