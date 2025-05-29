CC=g++

# Default build type is release
BUILD_TYPE ?= release

ifeq ($(BUILD_TYPE),debug)
    CPPFLAGS := -O0 -g -Wall
	override CPPFLAGS += -static -std=c++2a
else
    CPPFLAGS := -O3 -Wall
    override CPPFLAGS += -static -std=c++2a
endif

SRCS = test.cpp
OBJS = $(SRCS:%.cpp=%.o)

TARGET = test

.SUFFIXES : .cpp .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(INC) $(LIBS)

%.o: %.cpp
	$(CC) $(CPPFLAGS) $(INC) -o $@ -c $<
	
depend:
	gccmakedep $(SRCS)
   
clean:
	rm -f $(OBJS) $(TARGET)

