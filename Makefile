CC      := gcc

PIPEWIRE_CFLAGS := $(shell pkgconf --cflags libpipewire-0.3)
PIPEWIRE_LIBS   := $(shell pkgconf --libs libpipewire-0.3)
FFTW_LIBS				:= -lfftw3 -lm

CFLAGS  := -g -Wall -Wextra $(PIPEWIRE_CFLAGS)
LDFLAGS := $(PIPEWIRE_LIBS) $(FFTW_LIBS)

TARGET  := oto

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
