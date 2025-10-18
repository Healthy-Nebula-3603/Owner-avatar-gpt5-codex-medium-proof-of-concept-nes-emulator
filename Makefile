CC ?= cc
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wmissing-prototypes

SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

ifeq ($(SDL2_CFLAGS),)
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
endif

ifeq ($(strip $(SDL2_CFLAGS)),)
SDL2_CFLAGS :=
endif

ifeq ($(strip $(SDL2_LIBS)),)
SDL2_LIBS := -lSDL2
endif

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

TARGET := nes

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(SDL2_LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(SDL2_CFLAGS) -Isrc -Iinclude -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

run: $(TARGET)
	./$(TARGET)
