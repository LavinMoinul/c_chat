CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11
BIN_DIR=bin
SRC_DIR=src

SERVER=$(BIN_DIR)/server

all: $(SERVER)

$(SERVER): $(SRC_DIR)/server.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRC_DIR)/server.c

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
