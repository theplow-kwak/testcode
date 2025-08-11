CXX=g++
SRCS = test.cpp
TARGET = test

CXXFLAGS = -std=c++2a -Wall

RELEASE_FLAGS = -O2
DEBUG_FLAGS = -g -O0

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