CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = -lreadline
SRC = msh.c
OUT = msh

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: clean

