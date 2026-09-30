# Third-party notices

## LilyGO T-Dongle-S3 display initialization

The ST7735 initialization register values in `firmware/main/display.c` are
adapted from LilyGO's T-Dongle-S3 factory example, under the MIT license here:
https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/bb7654607d280fc2a1451d24abf6ed027287d416/examples/factory_screen/esp_lcd_st7735.c
The landscape offsets, pins, active-low backlight and orientation follow the
adjacent `factory_screen.ino`. The bounded SPI transport, renderer, glyphs and
status model are implemented in this repository; no GUI framework is required.

### License

MIT License

Copyright (c) 2022 Xinyuan-LilyGO

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Browser installer dependencies

The browser installer uses esptool-js (Apache-2.0), @noble/hashes (MIT),
and their dependencies under their respective licenses. Versions are pinned in
`installer/package-lock.json`. The website build includes the complete upstream
license texts in `THIRD_PARTY_LICENSES.txt`, linked from its footer. These
components are not relicensed under the project's MIT license.


## Installer button photo

`installer/assets/dongle-button.png` is the unmodified `images/product/png/T-Dongle-S3.png` from [Xinyuan-LilyGO/T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/bb7654607d280fc2a1451d24abf6ed027287d416/images/product/png/T-Dongle-S3.png), commit `bb7654607d280fc2a1451d24abf6ed027287d416`. Copyright (c) 2022 Xinyuan-LilyGO, MIT. The license is retained in `installer/assets/LILYGO-LICENSE.txt` and the built installer’s third-party licenses. The button marker is a separate CSS overlay.
