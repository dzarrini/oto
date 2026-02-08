CC      := gcc

PIPEWIRE_CFLAGS := $(shell pkgconf --cflags libpipewire-0.3)
PIPEWIRE_LIBS   := $(shell pkgconf --libs libpipewire-0.3)

CFLAGS  := -g -Wall -Wextra $(PIPEWIRE_CFLAGS)
LDFLAGS := $(PIPEWIRE_LIBS) -lfftw3 -lm

TARGET  := visualizer

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
