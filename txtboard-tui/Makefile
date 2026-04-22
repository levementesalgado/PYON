CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
          -Wno-unused-function -Wno-format -Wno-misleading-indentation \
          $(EXTRA_CFLAGS)
LDFLAGS = -lncursesw
SRC     = src/main.c src/home.c src/relay.c src/ui.c \
          src/boards.c src/board_view.c src/config.c
OUT     = pyon-tui

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

clean:
	rm -f $(OUT)

.PHONY: all clean
