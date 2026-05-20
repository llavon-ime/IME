#pragma once

#include <windows.h>

#include <cstdint>
#include <span>
#include <vector>

#include "core/bopomofo.hpp"
#include "engine.h"

namespace tsf {

class PipeEngine : public IEngine {
    static inline HANDLE hPipe = INVALID_HANDLE_VALUE;

    static bool ensure_pipe() {
        if (hPipe != INVALID_HANDLE_VALUE) return true;
        hPipe = CreateFileW(L"\\\\.\\pipe\\IME_Service", GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
        return hPipe != INVALID_HANDLE_VALUE;
    }

    static void disconnect() {
        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
    }

    template <typename T>
    static bool write_exact(const T& val) {
        DWORD written = 0;
        return WriteFile(hPipe, &val, sizeof(T), &written, nullptr) && written == sizeof(T);
    }

    static bool write_exact(const void* buf, DWORD size) {
        DWORD written = 0;
        while (written < size) {
            DWORD n = 0;
            if (!WriteFile(hPipe, static_cast<const char*>(buf) + written, size - written, &n, nullptr)) return false;
            written += n;
        }
        return true;
    }

    template <typename T>
    static bool read_exact(T& val) {
        DWORD total = 0;
        while (total < sizeof(T)) {
            DWORD n = 0;
            if (!ReadFile(hPipe, reinterpret_cast<char*>(&val) + total, sizeof(T) - total, &n, nullptr) || n == 0)
                return false;
            total += n;
        }
        return true;
    }

    static bool read_exact(void* buf, DWORD size) {
        DWORD total = 0;
        while (total < size) {
            DWORD n = 0;
            if (!ReadFile(hPipe, static_cast<char*>(buf) + total, size - total, &n, nullptr) || n == 0) return false;
            total += n;
        }
        return true;
    }

public:
    void predict(const std::u16string& context, std::span<BopomofoPos> padding) override {
        if (!ensure_pipe()) return;

        // --- request ---
        uint32_t ctx_len = static_cast<uint32_t>(context.size());
        if (!write_exact(ctx_len)) { disconnect(); return; }
        if (ctx_len > 0 && !write_exact(context.data(), ctx_len * sizeof(char16_t))) { disconnect(); return; }

        uint32_t pad_cnt = static_cast<uint32_t>(padding.size());
        if (!write_exact(pad_cnt)) { disconnect(); return; }

        for (auto& p : padding) {
            uint8_t type = p.chosen ? 1 : 0;
            if (!write_exact(type)) { disconnect(); return; }

            if (p.chosen) {
                char32_t c = p.current32();
                if (!write_exact(c)) { disconnect(); return; }
            } else {
                auto bpmf = p.to_bopomofo_string();
                uint32_t len = static_cast<uint32_t>(bpmf.size());
                if (!write_exact(len)) { disconnect(); return; }
                if (len > 0 && !write_exact(bpmf.data(), len * sizeof(char16_t))) { disconnect(); return; }
            }
        }

        // --- response ---
        uint32_t resp_cnt;
        if (!read_exact(resp_cnt)) { disconnect(); return; }

        for (uint32_t i = 0; i < resp_cnt && i < static_cast<uint32_t>(padding.size()); i++) {
            uint32_t cand_cnt;
            if (!read_exact(cand_cnt)) { disconnect(); return; }

            std::vector<char32_t> cands(cand_cnt);
            if (cand_cnt > 0 && !read_exact(cands.data(), cand_cnt * sizeof(char32_t))) { disconnect(); return; }

            if (!padding[i].chosen) {
                padding[i].set_candaiates(std::move(cands));
            }
            padding[i].predicted = true;
        }
    }
};

}  // namespace tsf
