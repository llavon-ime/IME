#pragma once
#include <filesystem>
#include <fstream>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "jsoncons/json.hpp"
#include "utf8cpp/utf8/cpp20.h"
#include "utils/debugSink.hpp"

namespace tsf {

class WordMappingEngine {
    // std::wstring 實際上代表一個漢字 為了多碼點字(BMP外)
    std::unordered_map<std::wstring, std::vector<std::wstring>> mapping;

public:
    static WordMappingEngine& instance() {
        static WordMappingEngine engine;
        return engine;
    }
    std::wstring lookup(const std::wstring& bopomofo) {
        if (mapping.contains(bopomofo) && !mapping[bopomofo].empty()) {
            return mapping[bopomofo][0];
        } else {
            // temporary fallback
            DebugSink::instance().send(L"MSG", bopomofo);
            return L"操";
        }
    }

private:
    static std::filesystem::path resolve_mapping_file(
        std::source_location loc = std::source_location::current()) {
        std::filesystem::path this_file = std::filesystem::path(loc.file_name()).lexically_normal();
        if (this_file.is_relative()) {
            this_file = std::filesystem::absolute(this_file).lexically_normal();
        }

        // tsf/src/core/bopomofoBuffer.hpp -> project root
        std::filesystem::path project_root = this_file.parent_path().parent_path().parent_path().parent_path();
        std::filesystem::path mapping_file = project_root / "tables" / "bopomofo_char.json";
        if (!std::filesystem::exists(mapping_file)) {
            throw std::runtime_error("Mapping file not found: " + mapping_file.string());
        }
        return mapping_file;
    }

    WordMappingEngine() {
        DebugSink::instance().send(L"LOAD", L"Loading mapping...");
        std::filesystem::path mapping_file = resolve_mapping_file();
        // mapping = glaze::read_json<std::unordered_map<std::wstring, std::vector<std::wstring>>>(mapping_file);
        std::ifstream ifs(mapping_file.string());
        jsoncons::json j = jsoncons::json::parse(ifs);
        auto temp = j.as<std::unordered_map<std::string, std::vector<std::string>>>();
        for (auto& [k, v] : temp) {
            auto u16key = utf8::utf8to16(k);
            std::wstring wkey(u16key.begin(), u16key.end());
            std::vector<std::wstring> wvec;
            for (const auto& item : v) {
                auto u16item = utf8::utf8to16(item);
                std::wstring witem(u16item.begin(), u16item.end());
                wvec.push_back(witem);
            }
            mapping[wkey] = std::move(wvec);
        }
    }
};

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
        return WordMappingEngine::instance().lookup(bopomofo);
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
        // assert(ch is bopomofo)
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
};

}  // namespace tsf
