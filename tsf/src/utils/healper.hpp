#pragma once

#include <windows.h>
#include <winrt/base.h>

#include <functional>
#include <source_location>
#include <stdexcept>
#include <string>

#include "debugSink.hpp"


namespace tsf {

namespace win {
struct check {
    constexpr explicit check(std::source_location location = std::source_location::current()) : location(location) {}

    std::source_location location;
};
}  // namespace win

/**
 * @brief Helper operator for checking HRESULT values and logging errors.
 * example usage: `SomeFunction() | win::check{};`
 */
inline void operator|(HRESULT hr, const win::check& checker) {
    if (FAILED(hr)) {
        const std::source_location& location = checker.location;
        const std::string file = location.file_name();
        const std::string function = location.function_name();

        const std::wstring detail =
            L"HRESULT failure: " + std::to_wstring(hr) + L" at " + std::wstring(file.begin(), file.end()) + L":" +
            std::to_wstring(location.line()) + L" in " + std::wstring(function.begin(), function.end());

        DebugSink::instance().send(L"ERROR", detail);

        const std::string message =
            "HRESULT failure: " + std::to_string(hr) + " at " + file + ":" + std::to_string(location.line()) + " in " +
            function;
        throw std::runtime_error(message);
    }
}

class before_return {
public:
    before_return(std::function<void()> func) : func_(std::move(func)) {}
    ~before_return() {
        if (func_) func_();
    }

private:
    std::function<void()> func_;
};

}  // namespace tsf