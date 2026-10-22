# AnimatedGIF 2.2.3 for KAC

Copy of [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) 2.2.3 (Apache License 2.0, see LICENSE),
changed to save RAM on the ESP8266 (the decoder needs one contiguous block):

- `MAX_WIDTH` 240 instead of 480 (display width; wider GIFs are rejected) – saves 240 bytes
- palettes `pPalette`/`pLocalPalette` hold RGB565 only (256 entries instead of 384) – saves 512 bytes;
  `begin()` therefore accepts only `GIF_PALETTE_RGB565_LE`/`_BE`
- removed: assembler helpers for ESP32-S3/P4 and `GIFPlayer.h` (not used)
