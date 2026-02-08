# oto

`oto` is a terminal audio visualizer written in C. It captures live audio using PipeWire, runs a real-time FFT, and renders simple bass/mid/treble bars in the terminal.

The name **Oto** comes from the Japanese word for sound (音, "oto").

## What It Does

- Captures audio from PipeWire as `F32_LE` floating-point samples.
- Buffers `2048` frames and applies a Hann window to reduce spectral leakage.
- Runs a real-to-complex FFT with FFTW (`fftw_plan_dft_r2c_1d`).
- Computes magnitudes per frequency bin.
- Aggregates energy into three coarse bands: bass (`20-250 Hz`), mid (`250-2000 Hz`), and treble (`2000-8000 Hz`)
- Tracks peak values with a decay factor and prints bar/peak data on one terminal line.

## Computation Notes

The current processing loop is synchronous in the PipeWire stream callback:

1. Pull buffer from PipeWire.
2. Copy one channel into the time-domain ring (`timebuf`).
3. Every `FFT_FRAMES` samples, run windowing + FFT + magnitude + band extraction.
4. Print the visualization with `printf` + `fflush`.
5. Return buffer to PipeWire.

This keeps the implementation compact, but heavy computation and terminal I/O happen in the real-time processing path.

## PipeWire Integration

`oto` uses `pw_stream_new_simple` with:

- `PW_DIRECTION_INPUT`
- `PW_STREAM_FLAG_AUTOCONNECT`
- `PW_STREAM_FLAG_MAP_BUFFERS`
- `PW_STREAM_FLAG_RT_PROCESS`

It subscribes to `param_changed` for negotiated format/rate/channels and `process` for per-buffer sample handling.

## Build & Run

Requirements:

- `gcc`
- PipeWire dev package (`libpipewire-0.3` via `pkgconf`)
- FFTW (`libfftw3`)

Dependencies (development headers/libraries):

- PipeWire: `libpipewire-0.3` (`pipewire` + development package)
- FFTW3: `fftw3` (`libfftw3` + development package)
- `pkgconf`/`pkg-config` for compiler/linker flags

Commands:

```bash
make
make run
```

Clean:

```bash
make clean
```

## TODO

- Migrate visualization rendering to `ncurses` for cleaner, flicker-resistant terminal output.
- Avoid blocking the active PipeWire processing thread with expensive computations and repeated `printf`/`fflush` calls.
- Move FFT + rendering work to another thread and keep the audio callback minimal.
- Add a historical buffer (time-series history) to track band energy changes over time for richer visualization.

## Linux Package Management

Install dependencies with your distro package manager:

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libpipewire-0.3-dev libfftw3-dev
```

### Fedora

```bash
sudo dnf install -y gcc make pkgconf-pkg-config pipewire-devel fftw-devel
```

### Arch Linux

```bash
sudo pacman -S --needed base-devel pkgconf pipewire fftw
```

## Third-Party Attribution

This project uses FFTW3.

- FFTW is Copyright (C) 2003, 2007-11 Matteo Frigo
- FFTW is Copyright (C) 2003, 2007-11 Massachusetts Institute of Technology
- FFTW license: GNU General Public License (GPL), version 2 or later

Reference: https://www.fftw.org/

## References

- PipeWire: https://pipewire.org/
- Chroma (reference project): https://github.com/yuri-xyz/chroma

## LLM Usage Disclosure

LLMs were used only for:

- helping inspect/debug current output and build behavior
- drafting and refining this `README`
- helping clarify licensing/compliance notes

LLMs were **not** used to write the actual application source code.

## License

This repository is licensed under **GNU GPL v2 or later** (`GPL-2.0-or-later`).
See `LICENSE` for the full license text and `NOTICE` for attribution notes.
