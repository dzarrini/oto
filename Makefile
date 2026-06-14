CC      := gcc

PIPEWIRE_CFLAGS := $(shell pkg-config --cflags libpipewire-0.3)
PIPEWIRE_LIBS := $(shell pkg-config --libs libpipewire-0.3)
FFTW_LIBS := $(shell pkg-config --libs fftw3)
LDFLAGS := $(shell pkg-config --libs ncurses)
# needed for fftw
EXTRA := -lm

CFLAGS  := -g -Wall -Wextra $(PIPEWIRE_CFLAGS)
LDFLAGS := $(PIPEWIRE_LIBS) $(FFTW_LIBS) $(LDFLAGS) $(EXTRA)

TARGET  := oto

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
