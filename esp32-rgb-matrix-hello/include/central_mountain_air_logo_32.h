#pragma once

// Crisp 1-bit 32px raster of the supplied Central Mountain Air mountain and
// "cma" mark. The source's white background is transparent. At matrix scale,
// using the carrier red keeps the small wordmark legible against the black panel.
constexpr uint8_t kCentralMountainAirBrandColor[3] = {228, 36, 28};  // #E4241C
constexpr uint32_t kCentralMountainAirLogoMask[32] PROGMEM = {
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00180000UL, 0x001C0000UL, 0x003E0000UL,
    0x00620000UL, 0x00C39800UL, 0x0780FC00UL, 0x0F80EC00UL,
    0x18000600UL, 0x30000000UL, 0x60007FFFUL, 0x6000DFBBUL,
    0xC000CDBFUL, 0x0000CDFFUL, 0x0000FDFFUL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
};
