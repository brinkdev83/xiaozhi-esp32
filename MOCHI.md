# Mochi-firmware

Bouwvariant `esp32-s3-touch-amoled-1.75-mochi` voor de Waveshare
ESP32-S3-Touch-AMOLED-1.75. Gelijk aan de standaardvariant, maar het bord
haalt zijn serverconfiguratie op bij een eigen xiaozhi-server op het thuisnetwerk
(`CONFIG_OTA_URL`) in plaats van bij xiaozhi.me. Zie de serverfork
`brinkdev83/xiaozhi-esp32-server` (branch `mochi`, `MOCHI.md`).

## Bewegingen

`main/boards/waveshare/esp32-s3-touch-amoled-1.75/motion_detector.h` leest de
QMI8658 (50 Hz) en herkent schudden (meerdere harde schokken binnen een
seconde) en oppakken (eerst 10 s stil, dan rustige beweging). Als het apparaat
niets doet, gaat dat via de wekwoordroute naar de server als
`[Mochi wordt door elkaar geschud]` of `[Mochi wordt opgepakt]`.

## Rond scherm

Het 466 px scherm is rond. `CustomLcdDisplay::FitRoundScreen()` zet de
pictogrammen en statusregel in het midden van de smalle bovenkant en toont de
chattekst meerregelig (`CONFIG_USE_MULTILINE_CHAT_MESSAGE`) in een smaller,
verhoogd vak onderin, zodat niets buiten de cirkel valt.

## Mochi op het scherm

`mochi_face.h` tekent Mochi met LVGL-vormen in plaats van de emoji: ademen,
knipperen, mond bewegen tijdens praten, sprietje geel tijdens luisteren, zzz
tijdens slapen en vlekjes als hij vies is. Het uiterlijk volgt de groeifase
(ei, baby, kind, volwassen) en de uitdrukking volgt Claude's emoji of anders
de stemming. Vier boogjes aan de zijkanten tonen de behoeften: links oranje
eten en geel energie, rechts roze plezier en blauw schoon. Is een behoefte laag,
dan knippert dat boogje en verschijnt er een denkwolkje ("Honger!",
"Slaap...", "Spelen?", "Badje!").

`MochiStatePoller` haalt elke 30 s `/mochi/state` op bij de server van de
OTA-URL. Wil Mochi aandacht en is het bord idle, dan start hij zelf een
gesprek met `[Mochi wil aandacht: ...]`.

## Bouwen en flashen (ESP-IDF release/v6.1)

```
python scripts/build.py waveshare/esp32-s3-touch-amoled-1.75 --name esp32-s3-touch-amoled-1.75-mochi --language nl-NL
idf.py -p COM5 flash
```

Krijgt de server-pc een ander IP-adres, pas dan `CONFIG_OTA_URL` aan in
`main/boards/waveshare/esp32-s3-touch-amoled-1.75/config.json` en bouw opnieuw.
