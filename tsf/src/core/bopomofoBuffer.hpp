#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "core/bopomofo.hpp"
#include "engine/engine.hpp"
#include "jsoncons/json.hpp"
#include "utf8cpp/utf8/cpp20.h"
#include "utils/debugSink.hpp"

namespace tsf {

struct Word {  // 漢字
    std::wstring bopomofo;
    std::wstring word;  // 用 wstring 為了處理 BMP 以外的字
    Word(std::wstring w, const std::wstring& b) : word(w), bopomofo(b) {}
};

class CompositionUnit {
    inline static const std::set<wchar_t> tones = {L' ', L'ˊ', L'ˇ', L'ˋ', L'˙'};
    std::wstring bopomofo;
    bool has_complete = false;

public:
    const std::wstring& get_bopomofo() const {
        return bopomofo;
    }
    void add(wchar_t ch) {
        if (has_complete) {
            throw std::runtime_error("Cannot add more characters to a completed composition unit");
        }
        if (tones.contains(ch)) {
            has_complete = true;
        }
        bopomofo.push_back(ch);
    }
    bool is_complete() const {
        return has_complete;
    }
    std::wstring composit() const {
        if (!has_complete) {
            throw std::runtime_error("Cannot composit an incomplete composition unit");
        }
        const auto candidates = WordMappingEngine::instance().lookup_all(bopomofo);
        auto top = Engine::instance().predict_next(candidates)[0];
        Engine::instance().add(top[0]);
        return top;
    }
};

class BopomofoBuffer {
    std::vector<std::variant<Word, CompositionUnit>> buffer;

public:
    std::wstring to_string() const {
        std::wstring result;
        for (const auto& item : buffer) {
            if (std::holds_alternative<Word>(item)) {
                result += std::get<Word>(item).word;
            } else {
                result += std::get<CompositionUnit>(item).get_bopomofo();
            }
        }
        return result;
    }
    void add(wchar_t ch) {
        // TODO: assert(ch is bopomofo)
        if (buffer.empty() || holds_alternative<Word>(buffer.back())) {
            buffer.push_back(CompositionUnit{});
        }
        CompositionUnit& composUnit = std::get<CompositionUnit>(buffer.back());
        composUnit.add(ch);
        if (composUnit.is_complete()) {
            std::wstring composited = composUnit.composit();
            buffer.back() = Word(composited, composUnit.get_bopomofo());
        }
    }
    void clear() {
        buffer.clear();
    }
    bool empty() const {
        return buffer.empty();
    }
    void pop_back() {
        if (buffer.empty()) return;
        buffer.pop_back();
    }
    std::variant<Word, CompositionUnit>& back() {
        return buffer.back();
    }
};

}  // namespace tsf
