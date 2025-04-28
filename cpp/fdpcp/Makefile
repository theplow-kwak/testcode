CC = gcc
CFLAGS = -O2 -Wall -pthread

TARGETS = fdp_copy_basic fdp_copy_stress_tool

all: $(TARGETS)

fdp_copy_basic: fdp_copy_basic.c
	$(CC) $(CFLAGS) -o $@ $^

fdp_copy_stress_tool: fdp_copy_stress_tool.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS) *.o
