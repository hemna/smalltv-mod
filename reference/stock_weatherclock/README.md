# Stock GeekMagic SmallTV-Ultra weather-clock assets

Extracted live from the stock device (192.168.0.240) running FW-Smalltv-Ultra-V9.0.50,
pulled over HTTP via its `/filelist?dir=` browser and direct GETs. NOT from the GitHub
firmware .bin (that ships code only; all these assets live on the device LittleFS).

## Astronaut sprite (`spaceman/`)

Served by the firmware at two URLs on the device:

| file | device path | size | frames | per-frame delay |
|------|-------------|------|--------|-----------------|
| spaceman_80x80.gif   | /gif/spaceman.gif   | 80x80 px, 27141 B  | 20 | 2 cs (0.02 s) => ~50 fps loop |
| spaceman_271x271.gif | /image/spaceman.gif | 271x271 px, 62892 B| 14 | 8 cs (0.08 s) => 12.5 fps loop |

The 80x80 GIF is the one that animates in the corner of the weather-clock screen.
- `frames_80/`  = 20 coalesced PNG frames (disposal already flattened)
- `frames_271/` = 14 coalesced PNG frames
- `spritesheet_80_20frames.png`  = horizontal strip, 20 x (80x80)
- `spritesheet_271_14frames.png` = horizontal strip, 14 x (271x271)

## Weather icons (`weather_icons/`)

Stored at the device FS root. Firmware fetches them by OpenWeatherMap icon code:
- day  = `/{code}.jpg`      (e.g. 01d.jpg)  -> rendered 60x60
- night= `/n{code}.jpg`     (e.g. n01d.jpg) -> stored 86x86

Codes are OpenWeatherMap's standard set. Confirmed visually:

| code | condition          | day (60x60) | night file (86x86) |
|------|--------------------|-------------|--------------------|
| 01d  | clear sky          | sun         | n01d.jpg (sun artwork, NOT moon) |
| 02d  | few clouds         | sun+cloud   | n02d.jpg |
| 03d  | scattered clouds   | cloud       | n03d.jpg |
| 04d  | broken clouds      | clouds      | n04d.jpg |
| 09d  | shower rain        | rain shower | n09d.jpg |
| 10d  | rain               | rain+sun    | n10d.jpg |
| 11d  | thunderstorm       | cloud+bolt  | n11d.jpg |
| 13d  | snow               | snowflake   | n13d.jpg |
| 50d  | mist/fog           | mist lines  | n50d.jpg |

NOTE (measured): the `n`-prefixed set is NOT moon-swapped. n01d/n02d still show the
sun artwork; the only difference from the day set is render size (86x86 vs 60x60).

## Clock font (`font/Alibaba20.vlw`)

TFT_eSPI `.vlw` bitmap font. Device path `/Alibaba20.vlw`. Header (big-endian):
- glyphCount = 325
- version    = 11
- fontSize   = 20 pt
- ascent     = 15, descent = 4
- total 57320 B

`.vlw` is TFT_eSPI's own smooth-font format (load with `tft.loadFont()` from LittleFS,
or embed via the FONT_FILE array). It is Alibaba PuHuiTi 20pt. This is the base UI font;
it is separate from the large clock digits (those are drawn with a GFXfont in the mod).

## Other

- `weather.html` = the stock device's weather-settings web page (gunzipped).
