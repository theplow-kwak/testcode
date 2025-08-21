# =============================================================================
# Makefile for the co_copy program
#
# Targets:
#   make all        (or `make`): Builds the release version. (default)
#   make release    Builds the optimized release version.
#   make debug      Builds the debug version with symbols.
#   make clean      Removes all compiled files.
# =============================================================================

CXX = g++
SRCS = co_copy.cpp

CXXFLAGS = -std=c++2a -Wall -static
LIBS = -luring -lnvme

TARGET = co_copy

RELEASE_FLAGS = -O2
DEBUG_FLAGS = -g -O0 -DDEBUG

.PHONY: all release debug clean

all: release

release: $(SRCS)
	@echo "Building release version..."
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $(TARGET) $(SRCS) $(LIBS)
	@echo "Release build finished: $(TARGET)"

debug: $(SRCS)
	@echo "Building debug version..."
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $(TARGET) $(SRCS) $(LIBS)
	@echo "Debug build finished: $(TARGET)"

clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(TARGET) *.o
	@echo "Cleanup finished."