#pragma once

#include <windows.h>

#include <optional>
#include <unordered_map>

namespace tsf {

class Bopomofo {
private:
    static constexpr int VK_0 = 0x30;
    static constexpr int VK_1 = 0x31;
    static constexpr int VK_2 = 0x32;
    static constexpr int VK_3 = 0x33;
    static constexpr int VK_4 = 0x34;
    static constexpr int VK_5 = 0x35;
    static constexpr int VK_6 = 0x36;
    static constexpr int VK_7 = 0x37;
    static constexpr int VK_8 = 0x38;
    static constexpr int VK_9 = 0x39;

    static constexpr int VK_A = 0x41;
    static constexpr int VK_B = 0x42;
    static constexpr int VK_C = 0x43;
    static constexpr int VK_D = 0x44;
    static constexpr int VK_E = 0x45;
    static constexpr int VK_F = 0x46;
    static constexpr int VK_G = 0x47;
    static constexpr int VK_H = 0x48;
    static constexpr int VK_I = 0x49;
    static constexpr int VK_J = 0x4A;
    static constexpr int VK_K = 0x4B;
    static constexpr int VK_L = 0x4C;
    static constexpr int VK_M = 0x4D;
    static constexpr int VK_N = 0x4E;
    static constexpr int VK_O = 0x4F;
    static constexpr int VK_P = 0x50;
    static constexpr int VK_Q = 0x51;
    static constexpr int VK_R = 0x52;
    static constexpr int VK_S = 0x53;
    static constexpr int VK_T = 0x54;
    static constexpr int VK_U = 0x55;
    static constexpr int VK_V = 0x56;
    static constexpr int VK_W = 0x57;
    static constexpr int VK_X = 0x58;
    static constexpr int VK_Y = 0x59;
    static constexpr int VK_Z = 0x5A;

    inline static const std::unordered_map<int, wchar_t> vkToBopomofo{{
        // Standard Zhuyin keyboard layout.
        {VK_1, L'ㄅ'},
        {VK_2, L'ㄉ'},
        {VK_5, L'ㄓ'},
        {VK_8, L'ㄚ'},
        {VK_9, L'ㄞ'},
        {VK_0, L'ㄢ'},
        {VK_OEM_MINUS, L'ㄦ'},

        {VK_Q, L'ㄆ'},
        {VK_W, L'ㄊ'},
        {VK_E, L'ㄍ'},
        {VK_R, L'ㄐ'},
        {VK_T, L'ㄔ'},
        {VK_Y, L'ㄗ'},
        {VK_U, L'ㄧ'},
        {VK_I, L'ㄛ'},
        {VK_O, L'ㄟ'},
        {VK_P, L'ㄣ'},

        {VK_A, L'ㄇ'},
        {VK_S, L'ㄋ'},
        {VK_D, L'ㄎ'},
        {VK_F, L'ㄑ'},
        {VK_G, L'ㄕ'},
        {VK_H, L'ㄘ'},
        {VK_J, L'ㄨ'},
        {VK_K, L'ㄜ'},
        {VK_L, L'ㄠ'},
        {VK_OEM_1, L'ㄤ'},

        {VK_Z, L'ㄈ'},
        {VK_X, L'ㄌ'},
        {VK_C, L'ㄏ'},
        {VK_V, L'ㄒ'},
        {VK_B, L'ㄖ'},
        {VK_N, L'ㄙ'},
        {VK_M, L'ㄩ'},
        {VK_OEM_COMMA, L'ㄝ'},
        {VK_OEM_PERIOD, L'ㄡ'},
        {VK_OEM_2, L'ㄥ'},

        // Tone keys.
        {VK_SPACE, L' '},
        {VK_6, L'ˊ'},
        {VK_3, L'ˇ'},
        {VK_4, L'ˋ'},
        {VK_7, L'˙'},
    }};

public:
    static std::optional<wchar_t> lookup(int vk) {
        const auto it = vkToBopomofo.find(vk);
        if (it == vkToBopomofo.end()) return std::nullopt;
        return it->second;
    }
};

}  // namespace tsf
