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

## Starting at boot: AMY and amyboard.py

Install the provided `boot.py` once:

```bash
mpremote cp board/boot.py :boot.py
```

On every boot and soft reset it runs `amyboard_boot.boot()` (frozen in): it starts
AMY on core 1, then does what an AMYboard does at startup (tulipcc's
`_boot.py`): `amyboard.ensure_user_environment()`, `init_display()`, the default
patches, and hooks MIDI input up to `midi.add_callback()`. The sketch
(`/user/current/sketch.py`, with its `loop(tick)`) is queued to start once
MicroPython has brought USB up, which it does only after `boot.py` returns, so a
sketch that hangs can't take USB down with it. Each step reports failures and
carries on, so the REPL always comes up. Remove it with `mpremote rm :boot.py`.

The sketch isn't auto-started when a tool such as `mpremote` soft-resets the board
through the raw REPL (its output would land in the middle of the tool's
protocol); AMY still runs, and the sketch starts on the next real boot, Ctrl-D,
or editor write.

## Recovering from a bad sketch

A sketch that hangs or is very slow doesn't freeze the board:

- Sketch code (top level, `loop(tick)`, MIDI callbacks, the editor's `zP`
  commands) runs from MicroPython's scheduler with the scheduler unlocked, so USB,
  MIDI and the editor protocol keep working, and **Ctrl-C** interrupts it (a sketch
  restarted twice -- by the transfer-done hook and by the editor -- needs two).
- The editor's ping (`zI`) and reboot (`zB`) are answered on core 1 in plain C, so
  they work whatever Python is doing. **`zB0`** (what the editor sends for a wedged
  board) reboots *without* the sketch; `zB1` is a normal reboot; **`zB2`** reboots
  into BOOTSEL, so the board can be reflashed without touching it.
- A soft reset can't deadlock on AMY: if core 1 doesn't stop within 500 ms the board
  reboots instead, and `c_amy.crumbs()` afterwards shows where core 1 was.

## AMYboard firmware Python (amyboard.py)

The real `amyboard.py` runs here, with `tulip.py`, `midi.py`, `synth.py`,
`sequencer.py`, `arpegg.py`, `patches.py`, `tulip_queue.py`, `upysh.py` and the
accessory drivers (`sh1107.py`, `ssd1327.py`, `ads1115.py`), all
frozen unchanged from tulipcc. They live in `py/tulipcc/`; refresh them with
`scripts/sync_tulipcc_py.sh` (copies from a tulipcc checkout's `origin/main` and
records the commit in `py/tulipcc/TULIPCC_COMMIT`).

What they need from Tulip's C `_tulip` module comes from `py/_tulip.py`, backed by
`c_amy` for the parts driven by AMY on core 1 (`modules/c_amy/amy_rp2_tulip.c`):
sequencer callbacks (`seq_add_callback`, which runs `loop(tick)`), MIDI input for
Python (`midi_callback` / `midi_in`), the overload callback and
`bootloader_mode()`. AMY's hooks only mark these due on core 1; the 1 ms core-0
timer schedules the Python callbacks.

The board presents as `AMYBOARD` (`tulip.board()`), so amyboard.py drives the
AMYboard accessories itself over I2C -- see below. `amyboard.start_amy()`'s ESP32
setup (codec, MIDI-out pins, boot button) isn't used; `amyboard_boot` does the
rest of what it does.

## Display, encoders and CV (I2C)

`amyboard_boot` opens **I2C1 on GP2 (SDA) / GP3 (SCL)** at 400 kHz and hands it to
amyboard.py (whose `get_i2c()` would otherwise open I2C0, on the MIDI pins); the
pins are constants at the top of `py/amyboard_boot.py`. amyboard.py's own code
then finds and drives what's attached, exactly as on an AMYboard:

| Accessory | I2C address | API |
|---|---|---|
| SH1107 OLED (128x128) | 0x3C | `amyboard.display` (`text`, `fill`, `show`, ...) |
| SSD1327 OLED (128x128) | 0x3D | `amyboard.display` |
| Adafruit seesaw encoders (single / quad / ANO), M5Stack 8Encoder | 0x36-0x58, 0x41 | `amyboard.encoder()` -> `read(i)`, `button(i)`, `led(...)` |
| ADS1015 ADC (CV in) | 0x48 | `amyboard.cv_in(ch)` (volts, read on demand; 0 V if absent) |
| GP8413 DAC (CV out) | 0x58 | `amyboard.cv_out(volts, ch)` |

Tested with an SH1107 and an Adafruit quad encoder. Not done yet: streaming a
synth's output to the CV DAC (`set_cv_out` / `tulip.set_cv_synth` is a no-op),
and the background I2C task AMYboard uses for display writes (display updates
are plain I2C writes from Python; the drivers send only what changed).
`amyboard.scan_i2c()` lists what's on the bus.

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
`zP import amyboard; amyboard.<...>()`, answered by the real `amyboard.py`
(below): `report_version()` replies `V <YYYYMMDD-hash>` (`c_amy.version()`, the
build date and commit), and "Write to your AMYboard" writes and restarts
`/user/current/sketch.py`, reporting errors back to the editor.

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
| Accessory I2C SDA (I2C1) | 2 |
| Accessory I2C SCL (I2C1) | 3 |

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
| `c_amy.crumbs()` | `(s0, s1, s2, core1)`: watchdog scratch 0..2 as the *previous* boot left them, and what core 1 is doing now. `amy.live()` arms a 5 s watchdog, so a startup hang reboots and s0 = `0xA37000nn` shows the stage; s0 = `0xA37000E0` means a soft reset gave up waiting for core 1, with its activity in s1 |
| `c_amy.raw_repl()` | whether a tool is driving the raw REPL |

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
| `test_sketch_loop.py` | a sketch whose `loop(tick)` counts calls and plays notes; write it with `spss_host.py write test_sketch_loop.py /user/current/sketch.py` then `spss_host.py exec "import amyboard; amyboard.environment_transfer_done()"` and check `sketch.calls` (~14/s at 108 BPM) |
| `spss_host.py editor` | the editor's "Write to your AMYboard" sequence: version probe, `sketch.py` transfer, restart; then a sketch with an error, which should be reported |
| `hang_test.py hang` / `reboot` / `fix` | recovery from a sketch that hangs: write one via the editor protocol (ping should still answer), normal reboot with it in place (USB should come up), then write `test_sketch_loop.py` over it |
| `test_i2c_accessories.py` | shows the encoders' values on the OLED for 20 s while you turn and press them; reports which moved |
| `test_gp5.py` | raw activity on the MIDI-in pin, independent of the UART; if it shows no edges while you play, the problem is upstream of the chip |

## Status

Measured at the default 150 MHz: idle render load 6.5%, three Juno voices 34%, no
underruns. Writing to the filesystem costs about one dropout (core 1 is paused while
flash is written).

MIDI in works over both UART1 and USB: notes play the default synths (Juno on
channel 1, DX7 on 2, drums on 10).

Sketches from amyboard.com/editor run with `loop(tick)`; factory reset, the
overload report (`L`), error reports (`X`), version (`V`) and the `zB` reboot
modes work as on an AMYboard.

Display and encoders work over I2C (GP2/GP3).

Not done yet: CV streaming from a synth, audio input, MCLK output, splitting rendering across both cores.
