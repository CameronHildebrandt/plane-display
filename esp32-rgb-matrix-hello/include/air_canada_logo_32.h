#pragma once

// Crisp 1-bit, 32x32 version of the supplied Air Canada mark. White source
// pixels are transparent; every set bit is rendered in kAirCanadaBrandColor.
constexpr uint8_t kAirCanadaBrandColor[3] = {240, 20, 40};  // #F01428
constexpr uint32_t kAirCanadaLogoMask[32] PROGMEM = {
    0x00000000UL, 0x000ff000UL, 0x007ffe00UL, 0x00f00f00UL,
    0x03c003c0UL, 0x070180e0UL, 0x0e018070UL, 0x0c03c030UL,
    0x1807e018UL, 0x3807e01cUL, 0x3007e00cUL, 0x30c7e30cUL,
    0x77e7e7eeUL, 0x63ffffc6UL, 0x63ffffc6UL, 0x63ffffc6UL,
    0x63ffffc6UL, 0x607ffe06UL, 0x707ffe0eUL, 0x30ffff0cUL,
    0x30ffff0cUL, 0x38ffff1cUL, 0x18f99f18UL, 0x0c018030UL,
    0x0e018070UL, 0x070180e0UL, 0x03c183c0UL, 0x01e18780UL,
    0x007f7e00UL, 0x000f7000UL, 0x00010000UL, 0x00000000UL,
};
