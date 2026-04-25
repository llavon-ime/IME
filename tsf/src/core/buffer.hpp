#pragma once
#include <vector>

#include "bopomofo.hpp"
#include "engine/engine.hpp"
using namespace std::literals;

namespace tsf {

struct BopomofoPos {
    char16_t initial = 0, medial = 0, final = 0;
    char16_t tone = 0;
    int choose_index = 0;
    const std::vector<char32_t>* candidates = nullptr;

    // TODO temp
    std::optional<std::vector<char32_t>> predicted_candidate;
    bool accept(char16_t c) {
        if (candidates != nullptr) {
            return false;
        }
        if (Bopomofo::initial.contains(c)) {
            initial = c;
        } else if (Bopomofo::medial.contains(c)) {
            medial = c;
        } else if (Bopomofo::final.contains(c)) {
            final = c;
        } else if (Bopomofo::tone.contains(c)) {
            tone = c;
            candidates = HanziMapEngine::instance().lookup_all(to_bopomofo_string());
            if (candidates == nullptr) {
                return false;
            }
        }
        return true;
    }
    std::u16string current() const {
        if (candidates == nullptr) {
            return to_bopomofo_string();
        }
        char32_t c = candidates->at(choose_index);
        std::u16string s;
        utf8::append16(c, s);
        return s;
    }
    void engine_choose(const std::u16string& context) {
        if (context.size() == 0) return;
        DebugSink::instance().send(L"INFO", u"engine choose CTX: " + context);
        if (candidates == nullptr) {
            DebugSink::instance().send(L"ERROR", L"No candidates to choose from");
            throw std::runtime_error("No candidates to choose from");
        }
        // TODO temp
        EngineContext engine;
        engine.add_str(context);
        predicted_candidate = engine.predict_next(*candidates);
        candidates = &predicted_candidate.value();
        choose_index = 0;
    }

public:
    std::u16string to_bopomofo_string() const {
        std::u16string res;
        if (initial) res.push_back(initial);
        if (medial) res.push_back(medial);
        if (final) res.push_back(final);
        if (tone) res.push_back(tone);
        return res;
    }
    bool is_null() const {
        return initial == 0 && medial == 0 && final == 0 && tone == 0;
    }
    bool is_compositable() const {
        return candidates != nullptr;
    }
    void set_choose_index(int idx) {
        choose_index = idx;
    }
    const std::vector<char32_t>& get_candidates() const {
        if (candidates == nullptr) {
            throw std::runtime_error("No candidates available");
        }
        return *candidates;
    }
};

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
                buffer[idx].engine_choose(ctx);
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