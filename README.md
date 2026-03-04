# oto

`oto` is a terminal audio visualizer written in C. It captures live audio using PipeWire, runs a real-time FFT, and renders a bass visualization with `ncurses`.

The name **Oto** comes from the Japanese word for sound (音, "oto").

## What It Does

- Captures audio from PipeWire as `F32_LE` floating-point samples.
- Buffers `2048` frames and applies a Hann window to reduce spectral leakage.
- Runs a real-to-complex FFT with FFTW (`fftw_plan_dft_r2c_1d`).
- Computes magnitudes per frequency bin.
- Aggregates energy into coarse bands (currently bass only: `20-250 Hz`).
- Tracks peak values with a decay factor and renders a full-width bar visualization with `ncurses` (orange).
- Averages consecutive bass measurements per bar to smooth spikes.

## Computation Notes

The current processing loop is synchronous in the PipeWire stream callback:

1. Pull buffer from PipeWire.
2. Copy one channel into the time-domain ring (`timebuf`).
3. Every `FFT_FRAMES` samples, run windowing + FFT + magnitude + band extraction.
4. Render the visualization with `ncurses`.
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
- ncurses (`ncurses`)

Dependencies (development headers/libraries):

- PipeWire: `libpipewire-0.3` (`pipewire` + development package)
- FFTW3: `fftw3` (`libfftw3` + development package)
- ncurses: `ncurses` (development package)
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

## Current Limitations

- Visualization height is fixed (not tied to terminal height).
- Bass-only view (mid/treble bands currently disabled).
- Rendering and FFT run in the PipeWire callback, which can block.
- Exit is not yet fully graceful (cleanup improvements pending).

## TODO

- Make visualization height dynamic per terminal window.
- Add graceful exit/cleanup.
- Let users specify visualization color.
- Reintroduce mid/treble bands and multi-band view.
- Move FFT + rendering work to another thread and keep the audio callback minimal.
- Add a historical buffer (time-series history) to track band energy changes over time for richer visualization.

## Linux Package Management

Install dependencies with your distro package manager:

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libpipewire-0.3-dev libfftw3-dev libncurses-dev
```

### Fedora

```bash
sudo dnf install -y gcc make pkgconf-pkg-config pipewire-devel fftw-devel ncurses-devel
```

### Arch Linux

```bash
sudo pacman -S --needed base-devel pkgconf pipewire fftw ncurses
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
