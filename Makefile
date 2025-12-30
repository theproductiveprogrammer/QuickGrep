CC = gcc
CFLAGS = -O3 -Wall -Wextra
TARGET = gg
SRC = gg.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/$(TARGET)

test: $(TARGET)
	./test.sh

.PHONY: all clean install test
