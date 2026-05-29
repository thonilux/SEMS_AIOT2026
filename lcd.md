# LCD Hardware Analysis

This project will use a small monochrome graphic LCD similar to the display shown in the reference images.

Based on the visible drawing text and public references, the display is most likely:

```text
Model family: LX-12864B11 or compatible
Resolution:   128 x 64 pixels
Controller:   Sitronix ST7567
LCD type:     FSTN / STN monochrome graphic LCD
Interface:    SPI, likely 4-wire SPI
Connector:    12-pin FPC
Logic voltage: 3.3 V class
```

## Identification From Provided Images

The second image contains these important details:

```text
DOTS: 128x64
Display type: FSTN
Operating voltage: 8.7 V
Operating temperature: -10 C ~ 60 C
Storage temperature: -20 C ~ 70 C
Drive mode: 1/65 duty, 1/9 bias
Viewing direction: 6:00
Polarizer type: transmissive
IC: ST7567
Connector: FPC, 12 pins, 0.5 mm pitch
Model marking: LX-12864B11
```

The first image shows a module-sized LCD around:

```text
Visible seller dimension: 46 mm x 29 mm
Tail length: about 40 mm
```

The second mechanical drawing appears to show a larger full outline:

```text
Outline width: about 57 mm
Visible area width: about 54 mm
Active area width: about 49 mm
Visible area height: about 29 mm
Active area height: about 25 mm
```

The exact physical dimensions should be verified against the purchased part because seller photos and drawings sometimes mix similar 12864 modules.

## Public Reference Findings

Public references confirm that `LX-12864B11` is a 128x64 STN black/white LCD using an ST7567 controller, commonly supplied as a 12-pin SPI module.

Similar ST7567 128x64 modules use:

```text
Controller: ST7567
Interface: 4-wire SPI
Logic supply: 2.8 V to 3.6 V, typically 3.3 V
LCD duty: 1/64
LCD bias: 1/9
Connector: 12-pin FPC
```

Useful references:

- https://wiki.mcselec.com/bavr/ST7567_display_library_-_128x64
- https://www.buydisplay.com/download/manual/ERC12864-11_Datasheet.pdf
- https://datasheet4u.com/datasheet-pdf/Sitronix%20Technology/ST7567/pdf.php?id=694908
- https://esphome.io/components/display/st7567/

## Important Electrical Notes

### Logic Voltage

Use 3.3 V logic.

Do not drive this LCD with 5 V GPIO from an Arduino Uno-style board unless level shifting is used. ESP32 GPIO is already 3.3 V and is suitable for the logic interface.

### LCD Bias Voltage

The drawing mentions an operating voltage around 8.7 V. This usually refers to the LCD drive voltage generated internally by the controller/charge pump or required by the LCD segment drive, not the MCU GPIO logic voltage.

Do not directly feed 8.7 V into normal logic pins.

The actual FPC pinout must be confirmed before powering the LCD.

### Backlight

The reference image looks like a reflective/transmissive monochrome LCD. Some LX-12864B11 listings mention an external backlight module, while the shown FPC glass may not include a simple two-pin LED backlight.

Do not assume backlight pins are present unless the exact FPC pinout confirms them.

## Likely Pinout

A similar 12-pin ST7567 12864 SPI LCD datasheet lists this style of pinout:

| Pin | Signal | Meaning |
| --- | --- | --- |
| 1 | `/CS` | Chip select, active low |
| 2 | `/RST` | Hardware reset, active low |
| 3 | `A0` / `DC` | Command/data select |
| 4 | `SCL` | SPI clock |
| 5 | `SDA` | SPI data input / MOSI |
| 6 | `VDD` | Logic power supply |
| 7 | `VSS` | Ground |
| 8 | `V0` | LCD drive voltage node |
| 9 | `XV0` | LCD drive voltage node |
| 10 | `NC` | Do not connect |
| 11 | `NC` | Do not connect |
| 12 | `VG` | LCD drive voltage node |

This pinout is from a similar ST7567 12864 module family, not yet confirmed for the exact purchased LCD. The actual LX-12864B11 pinout should be checked with the seller datasheet before wiring.

## Recommended ESP32 Wiring

For firmware development, use SPI.

Suggested wiring:

| LCD Signal | ESP32 Signal | Suggested GPIO |
| --- | --- | --- |
| `SCL` | SPI CLK | GPIO18 |
| `SDA` | SPI MOSI | GPIO23 |
| `A0` / `DC` | Data/command | GPIO21 |
| `/CS` | Chip select | GPIO5 |
| `/RST` | Reset | GPIO22 |
| `VDD` | 3.3 V | 3V3 |
| `VSS` | Ground | GND |

If GPIO5 causes boot issues on the selected board, move chip select to another safe GPIO.

Avoid sharing these pins with:

- RS485 UART
- Relay output
- Boot strapping pins where possible
- Flash/PSRAM pins

## Firmware Library Direction

The best Arduino/PlatformIO library direction is:

```text
U8g2
```

Why:

- Supports many monochrome graphic LCD controllers.
- Good ESP32 Arduino compatibility.
- Provides text rendering, fonts, graphics primitives, and framebuffer options.
- Cleaner than writing ST7567 initialization and page addressing manually.

Candidate U8g2 constructors to test:

```cpp
U8G2_ST7567_ENH_DG128064_F_4W_HW_SPI
U8G2_ST7567_PI_132X64_F_4W_HW_SPI
U8G2_ST7565_ERC12864_ALT_F_4W_HW_SPI
```

The exact constructor may need testing because ST7567 modules often differ in memory offset, contrast, mirroring, and scan direction.

## Firmware Abstraction Recommendation

The application should not call U8g2 directly everywhere.

Use a display abstraction:

```text
DisplayManager
  -> LcdDriver interface
      -> St7567U8g2Driver
```

This keeps the firmware independent from the exact LCD library and makes it easier to replace the display later.

## Suggested Display Pages

The LCD is 128x64, so pages must be compact.

Recommended pages:

```text
Page 1: Voltage / Current
Page 2: Power / Energy
Page 3: Frequency / Power Factor
Page 4: Relay / Current Limit
Page 5: WiFi / MQTT
Page 6: Fault / Modbus Status
```

Example page layout:

```text
V 230.5V   I 10.5A
P 1420W    PF 0.98
E 15.23kWh
Relay ON   MQTT OK
```

## Risk Checklist

Before final PCB wiring:

- Confirm the exact FPC pinout from seller datasheet.
- Confirm whether the LCD needs external capacitors for ST7567 charge pump pins.
- Confirm whether V0, XV0, and VG should be left floating, connected to capacitors, or connected through a recommended bias circuit.
- Confirm whether a backlight is included.
- Confirm whether the FPC pitch is 0.5 mm.
- Confirm connector orientation, top-contact or bottom-contact.
- Confirm whether the display is mirrored or vertically flipped after initialization.
- Confirm contrast value in firmware.

## Practical Recommendation

This LCD is usable with ESP32, but it is less plug-and-play than common OLED modules.

Recommended path:

1. Buy a matching 12-pin 0.5 mm FPC breakout board.
2. Verify pinout with the exact seller datasheet.
3. Test with U8g2 over 4-wire SPI.
4. Add contrast, rotation, and mirror options to `DisplayConfig`.
5. Only after a working prototype, lock the pinout into the PCB.

For this project, the LCD should be treated as:

```text
Display type: ST7567 128x64 SPI monochrome LCD
Voltage:      3.3 V logic
Library:      U8g2
Interface:    4-wire SPI
Status:       Supported, pending exact pinout verification
```

