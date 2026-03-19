CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
SRC = src/main.c src/container.c src/namespace.c src/utils.c
TARGET = cellc

all: $(TARGET)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
