#pragma once

// Crisp two-colour 32px raster of the supplied Flair badge. The light-green
// rounded square and black wordmark retain their hard pixel edges.
constexpr uint8_t kFlairBrandColor[3] = {140, 150, 50};  // #8C9632
constexpr uint8_t kFlairLogoGreen[3] = {168, 251, 141};
constexpr uint32_t kFlairLogoMask[32][2] PROGMEM = {
    {0x07ffffe0UL,0x00000000UL}, {0x1ffffff8UL,0x00000000UL},
    {0x3ffffffcUL,0x00000000UL}, {0x7ffffffeUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xf8f7ffffUL,0x05000000UL},
    {0xf091ffffUL,0x05000000UL}, {0xfa01ffffUL,0x05a40000UL},
    {0xfa03ffffUL,0x00600000UL}, {0xffffffffUL,0x00000000UL},
    {0xf8fd8fffUL,0x07000000UL}, {0xf060096fUL,0x0f800000UL},
    {0xf040000fUL,0x0fa90890UL}, {0xf040842fUL,0x0f890890UL},
    {0xf07fffffUL,0x07000000UL}, {0xfdffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0xffffffffUL,0x00000000UL}, {0xffffffffUL,0x00000000UL},
    {0x7ffffffeUL,0x00000000UL}, {0x7ffffffcUL,0x00000000UL},
    {0x3ffffffcUL,0x00000000UL}, {0x0ffffff0UL,0x00000000UL},
};
