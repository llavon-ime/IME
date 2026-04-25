#pragma once
#include <llama-cpp.h>

#include <algorithm>
#include <print>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/bopomofo.hpp"
#include "utf8cpp/utf8/cpp20.h"
#include "utils/debugSink.hpp"
#include "utils/healper.hpp"

namespace tsf {

class Tokenizer {
    const llama_vocab *vocab;

public:
    // std::unordered_map<std::u16string, llama_token> token_mapping;
    std::unordered_map<char32_t, llama_token> char_mapping;
    std::unordered_map<char32_t, std::vector<llama_token>> prefix_mapping;
    Tokenizer(const llama_vocab *vocab) : vocab(vocab) {
        const int32_t vocab_cnt = llama_vocab_n_tokens(vocab);
        for (int i = 0; i < vocab_cnt; i++) {
            const llama_token token = i;
            const std::string u8text = llama_vocab_get_text(vocab, token);
            const std::u32string u32text = utf8::utf8to32(u8text);

            bool is_chinese = true;
            for (auto &c : u32text) {
                if (!HanziMapEngine::instance().contains(c)) {
                    is_chinese = false;
                    break;
                }
            }
            if (is_chinese) {
                prefix_mapping[u32text[0]].push_back(token);
                if (u32text.size() == 1) {
                    char_mapping[u32text[0]] = token;
                }
            }
        }
    }
    llama_token lookup(char32_t c) const {
        return char_mapping.at(c);
    }
    std::vector<llama_token> lookup_prefix(char32_t word) const {
        return prefix_mapping.at(word);
    }
};

class ModeleManager {
    const char *model_path = R"(E:\CODE_programming\.IME\models\gemma-3-270m.Q4_K_M.gguf)";
    const llama_model_ptr _model;
    const llama_vocab *const _vocab;
    Tokenizer _tokenizer;

private:
    ModeleManager()
        : _model(llama_model_load_from_file(model_path, llama_model_default_params())),
          _vocab(llama_model_get_vocab(_model.get())),
          _tokenizer(_vocab) {
        llama_backend_init();
        DebugSink::instance().send(L"INFO", L"Model loaded successfully");
    }

public:
    static void initialize() {
        (void)instance();
        DebugSink::instance().send(L"INFO", "INITIALIZE");
    }
    static ModeleManager &instance() {
        static ModeleManager e;
        return e;
    }
    Tokenizer &tokenizer() {
        return _tokenizer;
    }
    llama_model *model() {
        return _model.get();
    }
    const llama_vocab *vocab() {
        return _vocab;
    }
};

class EngineContext {
    const llama_context_ptr context;
    const Tokenizer &tokenizer = ModeleManager::instance().tokenizer();
    const llama_vocab *const vocab = ModeleManager::instance().vocab();

public:
    EngineContext()
        : context(llama_init_from_model(ModeleManager::instance().model(), llama_context_default_params())) {
        if (llama_vocab_get_add_bos(vocab)) {
            llama_token bos = llama_vocab_bos(vocab);
            add_token(bos);
        }
    };

private:
    struct Token_Prob {
        llama_token token;
        float prob;
    };

    std::vector<Token_Prob> masked_predict(const std::vector<llama_token> &candidate) {
        float *logits = llama_get_logits_ith(context.get(), -1);  // 最後一個 token 的 logits
        if (logits == nullptr) {
            DebugSink::instance().send(L"ERROR", L"llama_get_logits_ith error");
            throw std::runtime_error("llama_get_logits_ith error");
        }
        std::vector<float> candidate_logits(candidate.size());
        for (int i = 0; i < candidate.size(); i++) {
            candidate_logits[i] = logits[candidate[i]];
        }
        float max_logit = *std::ranges::max_element(candidate_logits);
        std::vector<float> exps(candidate.size());
        float sum = 0.0f;
        for (int i = 0; i < candidate.size(); ++i) {
            exps[i] = std::exp(candidate_logits[i] - max_logit);
            sum += exps[i];
        }
        std::vector<Token_Prob> probs;
        for (int i = 0; i < candidate.size(); ++i) {
            probs.push_back({(llama_token)candidate[i], exps[i] / sum});
        }
        std::sort(probs.begin(), probs.end(), [](const Token_Prob &a, const Token_Prob &b) {
            return a.prob > b.prob;
        });
        return probs;
    }

public:
    std::u16string context_buffer;
    void add_token(llama_token token) {
        llama_batch batch = llama_batch_get_one(&token, 1);
        int32_t res = llama_decode(context.get(), batch);
        if (res != 0) {
            DebugSink::instance().send(L"ERROR", L"llama_decode error");
            throw std::runtime_error(std::format("error occurss : {}", res));
        }
    }
    void add(wchar_t c) {
        llama_token token = tokenizer.lookup(c);
        add_token(token);
        context_buffer.push_back(c);
    }

    void add_str(const std::u16string &wstr) {
        DebugSink::instance().send(L"INFO", u"ADD_STR :" + wstr);
        if (wstr.size() == 0) return;
        std::vector<llama_token> tokens = tokenize(wstr);
        llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
        int32_t res = llama_decode(context.get(), batch);
        context_buffer.append_range(wstr);
        if (res != 0) {
            DebugSink::instance().send(L"ERROR", L"llama_decode error");
            throw std::runtime_error(std::format("error occurss : {}", res));
        }
    }

    std::vector<char32_t> predict_next(const std::vector<char32_t> &candidates) {
        std::vector<llama_token> candidate_tokens;
        for (auto &c : candidates) {
            const auto tokens = tokenizer.lookup_prefix(c);
            candidate_tokens.append_range(tokens);
        }
        const auto prob_res = masked_predict(candidate_tokens);
        std::vector<char32_t> res;
        std::vector<std::pair<std::u16string, float>> res_with_prob;
        std::set<char32_t> res_set;
        for (auto &[token, prob] : prob_res) {
            const std::string u8text = llama_vocab_get_text(vocab, token);
            const std::u32string u32text = utf8::utf8to32(u8text);
            if (!res_set.contains(u32text[0])) {
                res.push_back(u32text[0]);
                res_set.insert(u32text[0]);
            }

            const std::u16string u16text = utf8::utf8to16(u8text);
            res_with_prob.push_back({u16text, prob});
        }
        DebugSink::instance().send(L"INFO", L"context : "_u16 + context_buffer);
        DebugSink::instance().send(L"INFO", L"Predicted next candidates:");
        for (const auto &[w, prob] : res_with_prob) {
            DebugSink::instance().send(L"INFO", std::format(L"{} : prob={:.5f}", w, prob));
        }
        return res;
    }

    std::vector<llama_token> tokenize(const std::u16string &wstr) {
        using namespace std::literals;
        std::u16string u16str(wstr.begin(), wstr.end());
        std::string str = utf8::utf16to8(u16str);
        const auto needed = -llama_tokenize(vocab, str.c_str(), (int32_t)str.size(), nullptr, 0, false, false);
        std::vector<llama_token> tokens(needed);
        int32_t res = llama_tokenize(
            vocab, str.c_str(), (int32_t)str.size(), tokens.data(), (int32_t)tokens.size(), false, false);
        if (res < 0) {
            throw std::runtime_error("llama tokenize error n_tokens : "s + std::to_string(res));
        }
        return tokens;
    }
};
}  // namespace tsf