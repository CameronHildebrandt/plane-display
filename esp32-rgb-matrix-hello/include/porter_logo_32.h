#pragma once

// Crisp 1-bit Porter mark. The supplied navy background is transparent; its
// black owl details use Porter blue while the face remains white.
constexpr uint8_t kPorterBrandColor[3] = {17, 40, 85};  // #112855
constexpr uint32_t kPorterLogoMask[32][2] PROGMEM = {
    {0x00000000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
    {0x00000000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
    {0x00000000UL, 0x00000000UL}, {0x07ffffe0UL, 0x00000000UL},
    {0x02fffe40UL, 0x01000180UL}, {0x02ffff80UL, 0x01000000UL},
    {0x01ffff80UL, 0x00000000UL}, {0x01ffff80UL, 0x00000000UL},
    {0x03ffff80UL, 0x00000000UL}, {0x03ffffc0UL, 0x00000000UL},
    {0x070001c0UL, 0x00fffe00UL}, {0x060000e0UL, 0x01ffff00UL},
    {0x0c0000e0UL, 0x03ffff00UL}, {0x0c000070UL, 0x03ffff80UL},
    {0x1c300870UL, 0x03cff788UL}, {0x1c000078UL, 0x03ffff80UL},
    {0x1c000078UL, 0x01ffff00UL}, {0x1e0200f8UL, 0x01fdff00UL},
    {0x1f06c1f0UL, 0x00f93e00UL}, {0x0ffc7ff0UL, 0x00038000UL},
    {0x07fcffe0UL, 0x00010000UL}, {0x03ffffc0UL, 0x00000000UL},
    {0x00ffff00UL, 0x00000000UL}, {0x001ff800UL, 0x00000000UL},
    {0x0007c000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
    {0x00000000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
    {0x00000000UL, 0x00000000UL}, {0x00000000UL, 0x00000000UL},
};
