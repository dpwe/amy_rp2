# source this: put the arm-none-eabi toolchain and picotool on PATH.
# Defaults to the copies bundled with arduino-pico's Arduino IDE install;
# skip this if you already have them on PATH.
_ARDUINO_RP2=~/Library/Arduino15/packages/rp2040/tools
_GCC=$(ls -d $_ARDUINO_RP2/pqt-gcc/*/bin 2>/dev/null | tail -1)
_PICOTOOL=$(ls -d $_ARDUINO_RP2/pqt-picotool/* 2>/dev/null | tail -1)
[ -n "$_GCC" ] && export PATH="$_GCC:$PATH"
[ -n "$_PICOTOOL" ] && export PATH="$_PICOTOOL:$PATH"
unset _ARDUINO_RP2 _GCC _PICOTOOL
