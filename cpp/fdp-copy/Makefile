CC = gcc
CFLAGS = -O2 -Wall -I/usr/include/libnvme
LDFLAGS = -lnvme

TARGET = fdp_copy_super_benchmark_libnvme_allin

all: $(TARGET)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o *.csv
