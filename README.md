# amy_rp2

[AMY](https://github.com/shorepine/amy) running under MicroPython on the RP2350,
built and tested on a **Pimoroni Pico Plus 2** with a PCM5102 I2S DAC and UART MIDI
on the same pins as AMY's `AMY_MIDI_Synth` Arduino example.

```python
import amy
amy.live()                         # I2S + UART MIDI, default synths, startup bleep
amy.send(synth=1, note=60, vel=1)
```

AMY's own Python package is frozen into the firmware unchanged; it finds the
`c_amy` C module here the same way it finds the CPython one.

## Pins (defaults)

| Signal | GPIO |
|---|---|
| I2S BCLK | 8 |
| I2S LRCLK | 9 (always BCLK + 1) |
| I2S DOUT | 10 |
| MIDI out (UART1 TX) | 4 |
| MIDI in (UART1 RX) | 5 |

Override with `amy.live(bclk=..., dout=..., midi_in=..., midi_out=..., midi_uart=...)`.
`midi=False` disables UART MIDI. The other `live()` kwargs match AMY's CPython
binding: `chorus`, `reverb`, `echo`, `default_synths`, `partials`, `custom`,
`startup_bleep`, `max_oscs`, `max_voices`, `max_synths`.

## Build and flash

```bash
git clone --recursive https://github.com/dpwe/amy_rp2.git
cd amy_rp2
make -C micropython/mpy-cross
source env.sh && make -C micropython/ports/rp2 BOARD=RPI_PICO2 submodules
./build.sh
```

`env.sh` puts the arm-none-eabi toolchain and picotool bundled with arduino-pico on
`PATH`; skip it if you have your own. To flash, put the board in BOOTSEL (hold BOOT,
tap RESET, or `mpremote bootloader` from a running build) and:

```bash
picotool load -x build-PIMORONI_PICO_PLUS2/firmware.uf2
```

## How it works

- **Core 1 does all of AMY**: message parsing, rendering, MIDI UART polling and the
  I2S DMA. Core 0 (the MicroPython VM) only pushes wire messages into a lock-free
  single-producer/single-consumer ring (`modules/c_amy/amy_rp2.c`), so AMY's locks,
  which are no-ops on bare pico-sdk, are never needed.
- **I2S** uses the PIO program bundled with AMY (`src/pico-audio/audio_i2s.pio.h`)
  with double-buffered DMA. No MCLK is generated, which the PCM5102 doesn't need.
- **Memory**: the 8 MB PSRAM becomes the MicroPython heap; AMY mallocs from a 352 KB
  SRAM C heap, falling back to a 2 MB pool reserved in PSRAM
  (`modules/c_amy/amy_rp2_mem.c`).
- **`_thread` is disabled** (`MICROPY_PY_THREAD=0`): AMY owns core 1, and with
  threading off MicroPython's flash-write lockout correctly pauses it.
- AMY is built with `AMY_HOST_MIDI` (this module supplies the MIDI device layer) and
  `MALLOC_CAPS_DEFINED`; nothing in the AMY or MicroPython trees is modified.

## Diagnostics

| Call | Returns |
|---|---|
| `c_amy.underruns()` | audio blocks where DMA ran dry |
| `c_amy.mem()` | `(sram_used, sram_free, psram_used, psram_pool, failed_allocs)` |
| `c_amy.crumbs()` | startup breadcrumbs; `amy.live()` arms a 5 s watchdog, so a startup hang reboots the board and this shows where it stopped |

Test scripts: `test_chord.py` (render load, underruns), `test_fs.py` (flash writes
while playing), `test_pins.py` (samples the I2S pins on-chip, independent of wiring).
Run with `mpremote run <script>`.

## Status

Measured at the default 150 MHz: idle render load 6.5%, three Juno voices 34%, no
underruns. Writing to the filesystem costs about one dropout (core 1 is paused while
flash is written).

Not done yet: USB MIDI, audio input, MCLK output, splitting rendering across both cores.
