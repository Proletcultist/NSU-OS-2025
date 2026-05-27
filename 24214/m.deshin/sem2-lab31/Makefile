CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2 -DDEBUG
LDLIBS = -lsocket -lnsl

SRC = main.c util.c http.c cache.c client.c net.c
OBJ = $(SRC:%.c=build/%.o)

TARGET = build/proxy

all: build $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDLIBS)

build/%.o: %.c proxy.h build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build
