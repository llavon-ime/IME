#pragma once
#include <vector>

#include "bopomofo.hpp"
#include "engine/engine.hpp"
using namespace std::literals;

namespace tsf {

class Buffer {
protected:
    std::vector<BopomofoPos> buffer;

public:
    std::u16string to_string() const {
        std::u16string res;
        for (const auto& item : buffer) {
            res += item.current();
        }
        return res;
    }
    void clear() {
        buffer.clear();
    }
    bool empty() const {
        return buffer.empty();
    }
};

class CompositionBuffer {
    std::vector<BopomofoPos> buffer;
    std::mutex mutex;
    int idx = -1;

public:
    void pre() {
        if (idx <= 0) {
            buffer.insert(buffer.begin(), {});
        } else {
            idx--;
        }
    }
    void next() {
        idx++;
        if (idx == buffer.size()) {
            buffer.push_back({});
        }
    }
    void remove_cur() {
        if (idx == -1) {
            return;
        }
        buffer.erase(buffer.begin() + idx);
        idx--;
    }
    void clear() {
        buffer.clear();
        idx = -1;
    }
    bool empty() const {
        return buffer.empty();
    }
    void add(char16_t ch) {
        std::lock_guard lock(mutex);
        DebugSink::instance().send(L"INFO", ch);
        if (idx == -1 || !buffer[idx].accept(ch)) {
            idx++;
            // buffer.insert(buffer.begin() + idx, {}); 會炸 我操你媽的標準委員會
            buffer.insert(buffer.begin() + idx, BopomofoPos{});
            DebugSink::instance().send(L"INFO", std::to_string(idx) + " wtf " + std::to_string(buffer.size()));
            buffer[idx].accept(ch);
        } else {
            // current accept this ch
            if (buffer[idx].is_compositable()) {
                std::u16string ctx;
                for (int i = 0; i < idx; i++) {
                    ctx += buffer[i].current();
                }
                DebugSink::instance().send(L"INFO", u"CONTEXT : " + ctx);
                auto engine = get_engine();
                engine->predict(u"", std::span<BopomofoPos>(buffer.begin(), buffer.begin() + idx + 1));
            }
        }
    }
    BopomofoPos& cur() {
        if (idx < 0 || idx >= buffer.size()) {
            DebugSink::instance().send(L"ERROR", "out of range");
            throw std::runtime_error("out of range");
        }
        return buffer[idx];
    }
    std::u16string to_string() const {
        std::u16string res;
        for (const auto& item : buffer) {
            res += item.current();
        }
        return res;
    }
};

}  // namespace tsf