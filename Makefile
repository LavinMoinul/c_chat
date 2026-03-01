CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11
SANFLAGS=-Wall -Wextra -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11

BIN_DIR=bin
SRC_DIR=src

SERVER=$(BIN_DIR)/server
TEST=$(BIN_DIR)/line_parser_test

all: $(SERVER) $(TEST)

$(SERVER): $(SRC_DIR)/server.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRC_DIR)/server.c

$(TEST): $(SRC_DIR)/line_parser_test.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(TEST) $(SRC_DIR)/line_parser_test.c

run: $(SERVER)
	./$(SERVER) 5555

test: $(TEST)
	./$(TEST)

sanitize: | $(BIN_DIR)
	$(CC) $(SANFLAGS) -o $(SERVER) $(SRC_DIR)/server.c

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all run test sanitize clean
