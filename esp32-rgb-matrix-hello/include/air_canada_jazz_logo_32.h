#pragma once

// Crisp 1-bit 32px raster of the supplied Air Canada Jazz wordmark. White
// source background is transparent; set pixels use the Air Canada red stored
// in air_canada_logo_32.h.
constexpr uint32_t kAirCanadaJazzLogoMask[32] PROGMEM = {
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x38000000UL, 0x78000000UL,
    0x70000000UL, 0x1e000000UL, 0x1f100000UL, 0x0fff9ef8UL,
    0x0f9bfe30UL, 0x37b7cc70UL, 0x637ddffeUL, 0xce301e60UL,
    0xf8000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
};
