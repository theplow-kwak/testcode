CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = 
TARGET = fdpcopy_stress
SRCS = fdpcopy_stress.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)