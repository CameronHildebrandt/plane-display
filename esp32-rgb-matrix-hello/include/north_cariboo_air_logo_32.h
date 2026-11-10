#pragma once

// Crisp 32px rendering of North Cariboo Air's three northern-wind strokes.
constexpr uint8_t kNorthCaribooAirBrandColor[3] = {171, 158, 117};
constexpr uint32_t kNorthCaribooAirLogoMask[32] PROGMEM = {
    0x00000000UL, 0x00000000UL, 0x00000070UL, 0x000003f0UL,
    0x00001fc0UL, 0x0000ff00UL, 0x0007fc00UL, 0x003ff000UL,
    0x01ffc078UL, 0x0ffe07f0UL, 0x7fe03fc0UL, 0x0001ff00UL,
    0x000ffc00UL, 0x007ff000UL, 0x03ff81e0UL, 0x1ff81fc0UL,
    0x0001fe00UL, 0x001ff000UL, 0x01ff8000UL, 0x0ff80000UL,
    0x3f800000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
};
