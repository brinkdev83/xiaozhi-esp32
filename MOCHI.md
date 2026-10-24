# Mochi-firmware

Bouwvariant `esp32-s3-touch-amoled-1.75-mochi` voor de Waveshare
ESP32-S3-Touch-AMOLED-1.75. Gelijk aan de standaardvariant, maar het bord
haalt zijn serverconfiguratie op bij een eigen xiaozhi-server op het thuisnetwerk
(`CONFIG_OTA_URL`) in plaats van bij xiaozhi.me. Zie de serverfork
`brinkdev83/xiaozhi-esp32-server` (branch `mochi`, `MOCHI.md`).

## Bouwen en flashen (ESP-IDF release/v6.1)

```
python scripts/build.py waveshare/esp32-s3-touch-amoled-1.75 --name esp32-s3-touch-amoled-1.75-mochi --language nl-NL
idf.py -p COM5 flash
```

Krijgt de server-pc een ander IP-adres, pas dan `CONFIG_OTA_URL` aan in
`main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.json` en bouw opnieuw.
