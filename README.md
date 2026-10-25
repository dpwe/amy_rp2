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

## Starting AMY at boot

Nothing starts AMY by itself yet. To have it running from power-on (needed for
amyboard.com/editor to find the board), install the provided `boot.py`:

```bash
mpremote cp board/boot.py :boot.py
```

It runs `amy.live()` on every boot and soft reset (so `mpremote` sessions keep AMY
running too), and catches errors so the REPL stays reachable. Remove it with
`mpremote rm :boot.py`.

## USB MIDI

The board is always a composite USB device: the MicroPython REPL serial port plus
a class-compliant MIDI port named **AMYboard RP2350** (no driver needed on macOS,
Linux or Windows). It's there from power-on and survives soft resets.

- **In:** incoming USB MIDI goes to the same AMY parser as UART MIDI.
- **Out:** AMY's MIDI output goes to USB as well as the UART, including its replies
  to the AMYboard editor's sysex (e.g. the `zI` ping is answered `OK`).
- `amy.live(usb_midi=False)` / `midi=False` turn off USB / UART MIDI.

macOS remembers MIDI device names by USB identity. If this board previously showed
up under another name, delete that entry in Audio MIDI Setup (Window > Show MIDI
Studio, with the board unplugged) and plug it back in.

## amyboard.com/editor protocol

The editor talks to an AMYboard with sysex frames `F0 00 03 45 ... F7` over MIDI
(USB or UART). This board handles them like AMYboard's firmware does:

| Frame | Handling |
|---|---|
| `zI` | replies `OK` (the editor's "is a board there?" ping) |
| `zT<path>,<size>` + base64 chunks | writes the file to the board's filesystem (creating folders) |
| `zD<path>` | sends the file back as base64 sysex; `zD` alone dumps AMY's state |
| `zP<python>` | runs the Python on the board |
| `zB0` / `zB1` / `zB2` | reboot (skip sketch / normal / BOOTSEL) |
| anything else | an ordinary AMY message |

Every frame except `zI` and `zB` is acknowledged with `F0 00 03 45 'A' 'K' F7`
once it has been handled. The editor's own commands are mostly
`zP import amyboard; amyboard.<...>()`. For now an interim `amyboard` module
(`py/amyboard.py`, frozen in) answers them: `report_version()` replies
`V <YYYYMMDD-hash>` (`c_amy.version()`, the build date and commit), and after
"Write to your AMYboard" the new `/user/current/sketch.py` is run, with errors
reported back to the editor. Only a sketch's top-level code runs; `loop(tick)`
waits for the real `amyboard.py`.

`spss_host.py` speaks the same protocol from the host: `test` runs ping, exec,
file write and read-back; `write SRC DEST`, `read PATH` and `exec CODE` do one
thing each.

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
- **USB MIDI** (`modules/c_amy/amy_rp2_usb.c`): TinyUSB's MIDI class is compiled in
  (`CFG_TUD_MIDI=1`) and MicroPython's configuration descriptor is swapped for one
  with its CDC interfaces plus MIDI (`-Wl,--wrap=tud_descriptor_configuration_cb`),
  so the MicroPython tree is unmodified; `machine.USBDevice` is disabled. TinyUSB
  runs on core 0 from MicroPython's scheduler: received MIDI goes into the ring to
  core 1, and AMY's output is queued and written to TinyUSB by a callback a 1 ms
  timer schedules on core 0.
- **Editor sysex**: a scanner on core 1 (`amy_rp2.c`) pulls complete SPSS frames
  out of all MIDI input and queues them to core 0, where `amy_rp2_sysex.c` handles
  them in a MicroPython scheduler callback: file transfers, `zP`, `zD <file>`,
  `zI` and `zB` there (they need the filesystem or the VM and only touch AMY's
  transfer state), everything else on core 1 via the command ring, waiting until
  core 1 has processed it before the `AK`. AMY's file, exec, transfer-done and
  reboot hooks are implemented there too.
- **I2S** uses the PIO program bundled with AMY (`src/pico-audio/audio_i2s.pio.h`)
  with double-buffered DMA. No MCLK is generated, which the PCM5102 doesn't need.
- **Memory**: the 8 MB PSRAM becomes the MicroPython heap; AMY mallocs from a 352 KB
  SRAM C heap, falling back to a 2 MB pool reserved in PSRAM
  (`modules/c_amy/amy_rp2_mem.c`).
- **`_thread` is disabled** (`MICROPY_PY_THREAD=0`): AMY owns core 1, and with
  threading off MicroPython's flash-write lockout correctly pauses it.
- **Core 1's stack is 16-byte aligned.** The ARM ABI needs an 8-byte-aligned SP; with
  only 4, varargs doubles go wrong, so `printf("%f")` on core 1 printed garbage. AMY
  formats MIDI velocities that way, so every MIDI note-on arrived with velocity 0.
- AMY is built with `AMY_HOST_MIDI` (this module supplies the MIDI device layer) and
  `MALLOC_CAPS_DEFINED`; nothing in the AMY or MicroPython trees is modified.

## Diagnostics

| Call | Returns |
|---|---|
| `c_amy.underruns()` | audio blocks where DMA ran dry |
| `c_amy.peak()` | largest \|output sample\| (0..32767) since the last call |
| `c_amy.midi_debug()` | `(uart_bytes, messages, last_message, active_channel_mask, synth_mask)`; masks have bit *n* set for MIDI channel / synth *n* |
| `c_amy.log()` | what AMY has printed to stderr since the last call (its `fprintf`s are redirected here) |
| `c_amy.mem()` | `(sram_used, sram_free, psram_used, psram_pool, failed_allocs)` |
| `c_amy.stack_unused()` | bytes of core 1's 16 KB stack never touched (high-water mark) |
| `c_amy.crumbs()` | startup breadcrumbs; `amy.live()` arms a 5 s watchdog, so a startup hang reboots the board and this shows where it stopped |

Test scripts (run with `mpremote run <script>`):

| Script | Checks |
|---|---|
| `test_chord.py` | render load and underruns for a three-voice chord |
| `test_fs.py` | filesystem writes while audio plays |
| `test_pins.py` | samples the I2S pins on-chip, independent of the DAC wiring |
| `test_levels.py` | output peak for `amy.send()` vs injected MIDI notes (should match) |
| `test_midi_inject.py` | injects MIDI through the parser UART MIDI uses; PASS/FAIL from DOUT activity |
| `test_midi_keyboard.py` | play a keyboard into GP5 while it runs: bytes, messages and output peak every 2 s |
| `test_usb_midi.sh` | run on the host: starts AMY, plays notes and a sysex + 16-note burst into the board's USB MIDI port (`send_usb_midi.py`), checks the `zI` → `OK` reply (`ping_usb_midi.py`); needs `pip install mido python-rtmidi` |
| `test_echo.py` | turns on echo while a note plays; delay lines that don't fit in SRAM go to the PSRAM pool |
| `test_echo_psram.py` | asks for a 3 s echo (more than the PSRAM pool holds); AMY should log the failure and keep running |
| `spss_host.py test` | the editor's sysex protocol: `zI` ping, `zP` exec, `zT` file write and `zD` read-back, a wire message |
| `spss_host.py editor` | the editor's "Write to your AMYboard" sequence: version probe, `sketch.py` transfer, restart; then a sketch with an error, which should be reported |
| `test_gp5.py` | raw activity on the MIDI-in pin, independent of the UART; if it shows no edges while you play, the problem is upstream of the chip |

## Status

Measured at the default 150 MHz: idle render load 6.5%, three Juno voices 34%, no
underruns. Writing to the filesystem costs about one dropout (core 1 is paused while
flash is written).

MIDI in works over both UART1 and USB: notes play the default synths (Juno on
channel 1, DX7 on 2, drums on 10).

Not done yet: audio input, MCLK output, splitting rendering across both cores.
