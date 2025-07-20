CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lreadline -lncurses
SRC = msh.c
OUT = msh

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: clean

