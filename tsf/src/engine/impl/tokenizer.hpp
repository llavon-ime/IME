#pragma once

#include <utf8/cpp20.h>

#include <fstream>
#include <jsoncons/basic_json.hpp>
#include <span>
#include <string>
#include <unordered_map>

#include "core/bopomofo.hpp"

using std::literals::operator""s;

namespace tsf {
class Tokenizer {
    inline static const char* char_path = R"(E:\CODE_programming\.IME\tables\tokens\chars.json)";
    inline static const char* latin_path = R"(E:\CODE_programming\.IME\tables\tokens\latin.json)";
    inline static const char* special_path = R"(E:\CODE_programming\.IME\tables\tokens\special_tokens.json)";
    inline static const char* bpmf_path = R"(E:\CODE_programming\.IME\tables\tokens\bpmf.json)";
    std::unordered_map<std::string, int> char_table;
    std::unordered_map<std::string, int> latin_table;
    std::unordered_map<std::string, int> special_table;
    std::unordered_map<std::string, int> bpmf_table;
    Tokenizer() {
        {
            std::ifstream ifs(char_path);
            jsoncons::json j = jsoncons::json::parse(ifs);
            char_table = j.as<std::unordered_map<std::string, int>>();
        }
        {
            std::ifstream ifs(latin_path);
            jsoncons::json j = jsoncons::json::parse(ifs);
            latin_table = j.as<std::unordered_map<std::string, int>>();
        }
        {
            std::ifstream ifs(special_path);
            jsoncons::json j = jsoncons::json::parse(ifs);
            special_table = j.as<std::unordered_map<std::string, int>>();
        }
        {
            std::ifstream ifs(bpmf_path);
            jsoncons::json j = jsoncons::json::parse(ifs);
            bpmf_table = j.as<std::unordered_map<std::string, int>>();
        }
    }
    static bool is_alpha(int c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }
    static char to_lower(int c) {
        if (c >= 'A' && c <= 'Z') {
            return char(c ^ 0x20);
        }
        return char(c);
    }

public:
    static Tokenizer& instance() {
        static Tokenizer tokenizer;
        return tokenizer;
    }
    int map_char(char32_t c) {
        std::string s;
        utf8::append(c, s);
        if (char_table.contains(s)) {
            return char_table.at(s);
        } else {
            return -1;
        }
    }
    std::vector<int> tokenize(const std::u16string& context16, const std::span<BopomofoPos>& padding) {
        std::vector<int> res;
        res.push_back(special_table.at("<BOS>"));
        std::string context8 = utf8::utf16to8(context16);
        std::u32string context = utf8::utf8to32(context8);
        for (int i = 0; i < context.size(); i++) {
            std::string s;
            utf8::append(context[i], s);
            if (context[i] == U' ') {
                res.push_back(special_table.at("<SP>"));
            } else if (char_table.contains(s)) {
                res.push_back(char_table.at(s));
            } else if (is_alpha(context[i])) {
                std::string str;
                for (; i < context.size(); i++) {
                    if (is_alpha(context[i])) {
                        str.push_back(to_lower(context[i]));
                    } else {
                        i--;
                        break;
                    }
                }
                if (latin_table.contains(str)) {
                    res.push_back(latin_table.at(str));
                } else {
                    res.push_back(special_table.at("<LATIN>"));
                }
            } else {
                res.push_back(special_table.at("<UNK>"));
            }
        }
        for (auto& b : padding) {
            if (b.chosen) {
                char32_t c = b.current32();
                std::string s;
                utf8::append(c, s);
                if (char_table.contains(s)) {
                    res.push_back(char_table.at(s));
                } else {
                    res.push_back(special_table.at("<UNK>"));
                }
            } else {
                std::u16string s16 = b.to_bopomofo_string();
                std::string s8 = utf8::utf16to8(s16);
                s8 = "<" + s8 + ">";
                if (!bpmf_table.contains(s8)) {
                    throw std::logic_error("invalid bpmf");
                }
                res.push_back(bpmf_table.at(s8));
            }
        }
        res.push_back(special_table.at("<SEP>"));
        std::string debug_str;
        for (auto& x : res) {
            debug_str += std::to_string(x) + " ";
        }
        DebugSink::instance().send(L"INFO", u"TOKENS : "s + utf8::utf8to16(debug_str));
        return res;
    }
};
}  // namespace tsf
