#pragma once

// Crisp 1-bit Sunwest Aviation mark: first mask is the sampled red, second
// mask is the navy element; the supplied white background is transparent.
constexpr uint8_t kSunwestBrandColor[3] = {124, 19, 59};  // #7C133B
constexpr uint8_t kSunwestNavyColor[3] = {4, 43, 86};
constexpr uint32_t kSunwestLogoMask[32][2] PROGMEM = {
    {0x00000000UL, 0x00000000UL}, {0x00000000UL, 0x000ff000UL},
    {0x00000000UL, 0x003ffe00UL}, {0x00000000UL, 0x00ffff00UL},
    {0x00000000UL, 0x03ffffc0UL}, {0x00000000UL, 0x01fff9e0UL},
    {0x00000000UL, 0x007fe7f0UL}, {0x00000000UL, 0x003fcff0UL},
    {0x00000000UL, 0x001f8ff8UL}, {0x00000000UL, 0x00071ffcUL},
    {0x00000000UL, 0x00071ffcUL}, {0x00000000UL, 0x00031ffcUL},
    {0x78000000UL, 0x00011ffeUL}, {0x7f000000UL, 0x00010ffeUL},
    {0x7f800000UL, 0x00000ffeUL}, {0x7fc00000UL, 0x000007feUL},
    {0x7fe00000UL, 0x000003feUL}, {0x7ff00000UL, 0x000001feUL},
    {0x7ff08000UL, 0x0000007eUL}, {0x7ff08000UL, 0x00000000UL},
    {0x3ff8c000UL, 0x00000000UL}, {0x3ff0e000UL, 0x00000000UL},
    {0x3ff1f000UL, 0x00000000UL}, {0x1ff1f800UL, 0x00000000UL},
    {0x0fe3fc00UL, 0x00000000UL}, {0x0fcfff00UL, 0x00000000UL},
    {0x07ffffc0UL, 0x00000000UL}, {0x03ffffc0UL, 0x00000000UL},
    {0x00ffff00UL, 0x00000000UL}, {0x003ffc00UL, 0x00000000UL},
    {0x000ff000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
};
