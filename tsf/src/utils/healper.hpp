#pragma once

#include <windows.h>
#include <winrt/base.h>

#include <stdexcept>

#include "debugSink.hpp"

namespace tsf {

namespace win {
struct check {};
}  // namespace win

/**
 * @brief Helper operator for checking HRESULT values and logging errors.
 * example usage: `SomeFunction() | win::check{};`
 */
void operator|(HRESULT hr, win::check) {
    if (FAILED(hr)) {
        std::wstring msg = winrt::hresult_error(hr).message().c_str();
        DebugSink::instance().send(L"ERROR", L"HRESULT failure: " + std::to_wstring(hr) + L" - " + msg);
        throw std::runtime_error("HRESULT failure: " + std::to_string(hr));
    }
}

}  // namespace tsf