CXX      = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -pthread

LIBS = -lsfml-graphics -lsfml-window -lsfml-audio -lsfml-network -lsfml-system -lrt
# LIBS = $(shell sdl2-config --libs) -lrt
# LIBS = -lglfw -lGL -lrt
# LIBS = -lncurses -lrt

COMMON_SRCS = common/inventory.cpp

TARGETS = bin/arbiter bin/hip bin/asp

all: clean $(TARGETS)
	@echo Build complete.

bin:
	mkdir -p bin

bin/arbiter: arbiter/arbiter.cpp | bin
	$(CXX) $(CXXFLAGS) arbiter/*.cpp $(COMMON_SRCS) -o $@ $(LIBS)

bin/hip: hip/hip.cpp | bin
	$(CXX) $(CXXFLAGS) hip/*.cpp $(COMMON_SRCS) -o $@ $(LIBS)

bin/asp: asp/asp.cpp | bin
	$(CXX) $(CXXFLAGS) asp/*.cpp $(COMMON_SRCS) -o $@ $(LIBS)

clean:
	rm -rf bin

.PHONY: all clean
