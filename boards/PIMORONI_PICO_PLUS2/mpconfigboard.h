// Pimoroni Pico Plus 2 + AMY
#define MICROPY_HW_BOARD_NAME "Pimoroni Pico Plus 2 (AMY)"

// AMY owns core 1; _thread would also want it, and MicroPython's flash-write
// lockout only pauses core 1 correctly for us with threading off.
#define MICROPY_PY_THREAD (0)

// UART1 belongs to AMY's MIDI (TX GP4, RX GP5); UART0 keeps its defaults.
#define MICROPY_HW_UART1_TX (4)
#define MICROPY_HW_UART1_RX (5)
#define MICROPY_HW_UART1_CTS (-1)
#define MICROPY_HW_UART1_RTS (-1)

// Stop AMY on soft reset (Ctrl-D) so a fresh amy.live() starts clean.
extern void amy_rp2_soft_reset(void);
#define MICROPY_BOARD_START_SOFT_RESET() amy_rp2_soft_reset()
