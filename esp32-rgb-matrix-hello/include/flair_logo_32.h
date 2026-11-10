#pragma once

// A deliberately simple 16px-diameter Flair badge, centered in the 32px slot.
constexpr uint8_t kFlairBrandColor[3] = {140, 150, 50};  // #8C9632
constexpr uint32_t kFlairLogoMask[32] PROGMEM = {
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x0007e000UL, 0x001ff800UL, 0x003ffc00UL, 0x007ffe00UL,
    0x007ffe00UL, 0x00ffff00UL, 0x00ffff00UL, 0x00ffff00UL,
    0x00ffff00UL, 0x00ffff00UL, 0x00ffff00UL, 0x007ffe00UL,
    0x007ffe00UL, 0x003ffc00UL, 0x001ff800UL, 0x0007e000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
    0x00000000UL, 0x00000000UL, 0x00000000UL, 0x00000000UL,
};
