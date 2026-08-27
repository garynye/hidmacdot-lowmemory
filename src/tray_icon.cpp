#include "tray_icon.h"

#include <cstring>

namespace tray_icon {
namespace {

constexpr int kIconSize = 32;
constexpr WORD kPurpleRows[16] = {
    0x0000, 0x03F0, 0x0FF0, 0x1C00, 0x3800, 0x7000, 0x6000, 0x6000,
    0x6000, 0x6000, 0x7000, 0x3800, 0x1C00, 0x0FF0, 0x03F0, 0x0000,
};
constexpr WORD kRedRows[16] = {
    0x0000, 0x0000, 0x0000, 0x0E38, 0x0E38, 0x0E38, 0x0E38, 0x0FF8,
    0x0FF8, 0x0FF8, 0x0E38, 0x0E38, 0x0E38, 0x0000, 0x0000, 0x0000,
};

} // namespace

HICON Create(HINSTANCE instance) {
    BYTE andMask[kIconSize * kIconSize / 8];
    BYTE colorBits[kIconSize * kIconSize * 4]{};
    std::memset(andMask, 0xFF, sizeof(andMask));

    for (int y = 0; y < kIconSize; ++y) {
        const int sourceY = y / 2;
        for (int x = 0; x < kIconSize; ++x) {
            const WORD sourceBit = static_cast<WORD>(1u << (x / 2));
            const bool isRed = (kRedRows[sourceY] & sourceBit) != 0;
            const bool isPurple = !isRed && (kPurpleRows[sourceY] & sourceBit) != 0;
            if (!isRed && !isPurple) continue;

            andMask[y * 4 + x / 8] &= static_cast<BYTE>(~(0x80u >> (x % 8)));
            BYTE* pixel = &colorBits[(y * kIconSize + x) * 4];
            pixel[0] = isRed ? 0x44 : 0xF6;
            pixel[1] = isRed ? 0x44 : 0x5C;
            pixel[2] = isRed ? 0xEF : 0x8B;
            pixel[3] = 0xFF;
        }
    }

    return CreateIcon(instance, kIconSize, kIconSize, 1, 32, andMask, colorBits);
}

} // namespace tray_icon
