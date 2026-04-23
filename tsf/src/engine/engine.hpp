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

namespace tsf {

class Tokenizer {
    const llama_vocab *vocab;

public:
    // std::unordered_map<std::wstring, llama_token> token_mapping;
    std::unordered_map<wchar_t, llama_token> char_mapping;
    std::unordered_map<wchar_t, std::vector<llama_token>> prefix_mapping;
    Tokenizer(const llama_vocab *vocab) : vocab(vocab) {
        const int32_t vocab_cnt = llama_vocab_n_tokens(vocab);
        for (int i = 0; i < vocab_cnt; i++) {
            const llama_token token = i;
            const std::string u8text = llama_vocab_get_text(vocab, token);
            const std::u16string u16text = utf8::utf8to16(u8text);
            const std::wstring wtext(u16text.begin(), u16text.end());

            bool is_chinese = true;
            for (auto &c : wtext) {
                // TODO handle out of BMP
                // 因為 BMP 內的字都在 16bit 內 所以可以這樣搞
                if (!WordMappingEngine::instance().contains(c)) {
                    is_chinese = false;
                    break;
                }
            }
            if (is_chinese) {
                // token_mapping[wtext] = token;
                prefix_mapping[wtext[0]].push_back(token);
                if (wtext.size() == 1) {
                    char_mapping[wtext[0]] = token;
                }
            }
        }
    }
    llama_token lookup(wchar_t c) {
        return char_mapping[c];
    }
    std::vector<llama_token> lookup_prefix(wchar_t word) {
        return prefix_mapping[word];
    }
};

class Engine {
    const char *model_path = R"(E:\CODE_programming\LLM_TEST_CPP\models\gemma-3-270m.Q4_K_M.gguf)";
    const llama_model_ptr model;
    const llama_context_ptr context;
    const llama_vocab *vocab;
    Tokenizer tokenizer;
    Engine()
        : model(llama_model_load_from_file(model_path, llama_model_default_params())),
          context(llama_init_from_model(model.get(), llama_context_default_params())),
          vocab(llama_model_get_vocab(model.get())),
          tokenizer(vocab) {
        llama_backend_init();
        DebugSink::instance().send(L"INFO", L"Model loaded successfully");
    };

    struct Token_Prob {
        llama_token token;
        float prob;
    };

    std::vector<Token_Prob> masked_predict(const std::vector<llama_token> &candidate) {
        float *logits = llama_get_logits_ith(context.get(), -1);  // 最後一個 token 的 logits
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
    static Engine &instance() {
        static Engine e;
        return e;
    }

    // void add(std::vector<llama_token> &tokens) {
    //     llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
    //     int32_t res = llama_decode(context.get(), batch);
    //     llama_batch_free(batch);
    //     if (res != 0) {
    //         throw std::runtime_error(std::format("error occurss : {}", res));
    //     }
    // }

    void add(wchar_t c) {
        llama_token token = tokenizer.lookup(c);
        std::vector<llama_token> tokens{token};
        llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
        int32_t res = llama_decode(context.get(), batch);
        // llama_batch_free(batch);
        if (res != 0) {
            DebugSink::instance().send(L"ERROR", L"llama_decode error");
            throw std::runtime_error(std::format("error occurss : {}", res));
        }
    }

    std::vector<std::wstring> predict_next(const std::vector<std::wstring> &candidates) {
        std::vector<llama_token> candidate_tokens;
        for (auto &c : candidates) {
            const auto tokens = tokenizer.lookup_prefix(c[0]);
            candidate_tokens.append_range(tokens);
        }
        const auto prob_res = masked_predict(candidate_tokens);
        std::vector<std::wstring> res;
        std::vector<std::pair<std::wstring, float>> res_with_prob;
        std::set<wchar_t> res_set;
        for (auto &[token, prob] : prob_res) {
            const std::string u8text = llama_vocab_get_text(vocab, token);
            const std::u16string u16text = utf8::utf8to16(u8text);
            const std::wstring wtext(u16text.begin(), u16text.end());
            if (!res_set.contains(wtext[0])) {
                res.push_back(std::wstring{wtext[0]});
                res_with_prob.push_back({std::wstring{wtext[0]}, prob});
                res_set.insert(wtext[0]);
            }
        }
        DebugSink::instance().send(L"INFO", L"Predicted next candidates:");
        for (const auto &[w, prob] : res_with_prob) {
            DebugSink::instance().send(L"INFO", std::format(L"{} : prob={:.5f}", w, prob));
        }
        return res;
    }

    // std::vector<llama_token> tokenizer(const std::wstring &wstr) {
    //     using namespace std::literals;
    //     std::u16string u16str(wstr.begin(), wstr.end());
    //     std::string str = utf8::utf16to8(u16str);
    //     const auto needed = -llama_tokenize(vocab, str.c_str(), str.size(), nullptr, 0, false, false);
    //     std::vector<llama_token> tokens(needed);
    //     int32_t res = llama_tokenize(vocab, str.c_str(), str.size(), tokens.data(), tokens.size(), false, false);
    //     if (res < 0) {
    //         throw std::runtime_error("llama tokenize error n_tokens : "s + std::to_string(res));
    //     }
    //     return tokens;
    // }
};
}  // namespace tsf