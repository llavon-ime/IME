#pragma once

#include <string>
#include <string_view>

namespace ime::linux {

std::u32string utf8_to_u32(std::string_view input);
std::u16string utf8_to_u16(std::string_view input);
std::string u16_to_utf8(std::u16string_view input);
std::string char32_to_utf8(char32_t codepoint);
char32_t first_utf8_codepoint(std::string_view input);

}  // namespace ime::linux
