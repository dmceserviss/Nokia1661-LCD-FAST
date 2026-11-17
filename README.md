# This-is-fast-SPI-library-for-Nokia-displays-based-on-the-SPFD54124B-controller
The library was taken from the web site: https://sisoog.com and was modernized.
1. Added font size selection.
2. Added support for displaying Cyrillic characters.
3. Added drawing of a square, triangle, and circle.
4. The display started working quickly.
5. Added output of variables.
6. Added support for other display controllers for Nokia.

Optimized for the best performance for 16MHz AVR Arduino boards.

This is fast SPI library for displays based on the SPFD54124B controller,
it is also compatible with displays using other controllers found in various Nokia phones 
(such as the Nokia 1616, 1661, 1800, C1-00, C1-01, X1-01, 2330c, 2630, 2680, 3110c, 3500c, 5030, and others).

Tested with the following displays ID.
0x548066 - Controller: Orise SPFD54124B with the Nokia module's OEM firmware.
0x548466 - Controller: Ilitek ILI9163 (factory code Ilitek 0x54).
0x548366 - Controller: Ilitek ILI9163 (factory code Ilitek 0x54).
0xE38166 - Controller: Sitronix ST7735S with the Nokia module's OEM firmware (0xE3).
0xE39066 - Controller: Sitronix ST7735S with the Nokia module's OEM firmware (0xE3).
0x5C89F0 - Controller: Sitronix ST7735 / ST7735R / ST7735S (Sitronics factory code).
0x7F8F8B - Non-standard OEM code. Compatible with the base profile ST7735 / ILI9163.

Pins for connecting the display to an Arduino.

LCD:       Arduino:

SCK        13
SDA        11
CS         10
RST        9
