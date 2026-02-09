Put your TTF files here for runtime testing (LittleFS).

The TTF test screen auto-scans `/littlefs/fonts/*.ttf`, so you can just drop
files into this folder and `uploadfs`.

Preloaded fonts (for quick testing):
- f01.ttf (Andale Mono)
- f02.ttf (Montserrat-Medium)
- f03.ttf (Montserrat-Bold)
- f04.ttf (Ubuntu-Medium)
- f05.ttf (Lato-Regular)
- f06.ttf (unscii-8)
- f07.ttf (test_gpos_one)
- f08.ttf (font_montserrat_24)

Pixel-screen friendly additions (OFL; see `licenses/OFL.txt`):
- PressStart2P-Regular.ttf
- Silkscreen-Regular.ttf
- Tiny5-Regular.ttf
- VT323-Regular.ttf
- ShareTechMono-Regular.ttf
- PixelifySans[wght].ttf
; (Keep only what you want to test; the LittleFS partition is ~4.7MB in this project.)

Flash LittleFS:
  pio run -e hub75_idf -t uploadfs
