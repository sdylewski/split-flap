#!/bin/sh
# Flash a unit over USB. Tries the new bootloader (Optiboot) first, then the old one.
# usage: ./flash.sh [port]      (default: the first /dev/cu.usbserial-* / wchusbserial port)
cd "$(dirname "$0")"
PIO="${PIO:-$(command -v pio || echo ~/.local/bin/pio)}"
PORT="${1:-$(ls /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -1)}"
[ -z "$PORT" ] && { echo "No USB serial port found - is the unit plugged in?"; exit 1; }
echo "Flashing unit on $PORT ..."
if "$PIO" run -e nano -t upload --upload-port "$PORT" > /tmp/flappy-unit-upload.log 2>&1; then
  echo "OK - new bootloader (Optiboot), watchdog enabled"
elif "$PIO" run -e nano_old -t upload --upload-port "$PORT" >> /tmp/flappy-unit-upload.log 2>&1; then
  echo "OK - old bootloader (no watchdog)"
else
  echo "FAILED - see /tmp/flappy-unit-upload.log"; exit 1
fi
