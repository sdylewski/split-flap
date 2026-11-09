#!/bin/sh
# Compress the web UI (web/) into data/ for the LittleFS upload.
# Run this after editing anything in web/, then upload the filesystem
# (Arduino IDE 2: Cmd+Shift+P -> "Upload LittleFS to Pico/ESP8266/ESP32").
# The web server sends the .gz files with "Content-Encoding: gzip" automatically.
set -e
cd "$(dirname "$0")"
for f in web/*; do
  name=$(basename "$f")
  gzip -9 -n -c "$f" > "data/$name.gz"
  echo "data/$name.gz  $(wc -c < "$f") -> $(wc -c < "data/$name.gz") bytes"
done
