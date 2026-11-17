# ST7701 MIPI driver provenance

`esp_lcd_st7701_mipi.c` and the headers are from the local HachimoDock copy
(commit `9a22e0f`, `firmware/components/esp_lcd_st7701`), itself based on
Espressif's `esp_lcd_st7701` v2.0.2~2. The HachimoDock variant includes a
write-only MIPI option and a reset delay. Original copyright notices are kept;
the code is available under the Apache License 2.0 in `LICENSE`. This component
builds only the MIPI source with a project-specific CMake file.
