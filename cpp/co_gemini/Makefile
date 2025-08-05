# =============================================================================
# Makefile for the co_copy program
#
# Targets:
#   make all        (or `make`): Builds the release version. (default)
#   make release    Builds the optimized release version.
#   make debug      Builds the debug version with symbols.
#   make clean      Removes all compiled files.
# =============================================================================

# Compiler and source files
CXX = g++
SRCS = co_copy.cpp

# Common flags for all builds
# C++20 standard is required for coroutines
CXXFLAGS = -std=c++2a -Wall

# Libraries to link against
# -luring for io_uring, -lnvme for NVMe command structures
LIBS = -luring -lnvme

# --- Target Executables ---
# Release version will be named 'co_copy'
TARGET = co_copy

# --- Build-specific Flags ---
# Release flags: -O2 for optimization, -DNDEBUG to disable asserts
RELEASE_FLAGS = -O2 -DNDEBUG
# Debug flags: -g for debug symbols, -O0 for no optimization
DEBUG_FLAGS = -g -O0 -DDEBUG

# Phony targets are not real files
.PHONY: all release debug clean

# --- Build Rules ---

# Default target: build the release version
all: release

# Rule to build the release version
release: $(SRCS)
	@echo "Building release version..."
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $(TARGET) $(SRCS) $(LIBS)
	@echo "Release build finished: $(TARGET)"

# Rule to build the debug version
debug: $(SRCS)
	@echo "Building debug version..."
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $(TARGET) $(SRCS) $(LIBS)
	@echo "Debug build finished: $(TARGET)"

# Rule to clean up build files
clean:
	@echo "Cleaning up..."
	rm -f $(TARGET) $(TARGET) *.o
	@echo "Cleanup finished."