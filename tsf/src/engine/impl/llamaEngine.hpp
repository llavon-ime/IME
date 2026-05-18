#pragma once

#include <llama-cpp.h>
#include <utf8/cpp20.h>

#include <chrono>
#include <fstream>
#include <jsoncons/basic_json.hpp>
#include <span>
#include <stdexcept>
#include <vector>

#include "core/bopomofo.hpp"
#include "engine.h"
#include "tokenizer.hpp"
#include "utils/healper.hpp"

using std::literals::operator""s;

namespace tsf {
struct LlamaCtx : public IEngineCtx {
    std::vector<llama_token> ctx;
    ~LlamaCtx() {}
};

class ModeleManager {
    const char *model_path = R"(E:\CODE_programming\.IME\models\bopomofo-ime-llama-250m-Q4_K_M.gguf)";
    llama_model_ptr _model;
    const llama_vocab *_vocab;

private:
    ModeleManager() {
        llama_backend_init();
        _model.reset(llama_model_load_from_file(model_path, llama_model_default_params()));
        if (!_model) {
            DebugSink::instance().send(L"ERROR", L"Failed to load model");
            throw std::runtime_error("Failed to load model");
        }

        _vocab = llama_model_get_vocab(_model.get());
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
    llama_model *model() {
        return _model.get();
    }
    const llama_vocab *vocab() {
        return _vocab;
    }
    llama_context *new_context() {
        return llama_init_from_model(ModeleManager::instance().model(), llama_context_default_params());
    }
};

class LlamaEngine : public IEngine {
    const llama_context_ptr llama_ctx;
    std::vector<llama_token> ctx;

public:
    LlamaEngine() : llama_ctx(ModeleManager::instance().new_context()) {}
    ~LlamaEngine() {}

    void predict(const std::u16string &context, std::span<BopomofoPos> padding /* in out */) override {
        const auto start = std::chrono::steady_clock::now();
        before_return log_predict_time([start]() {
            const auto end = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            DebugSink::instance().send(L"INFO", std::format("predict took {} ms", elapsed));
        });

        // TEMP
        llama_memory_clear(llama_get_memory(llama_ctx.get()), true);
        ctx = Tokenizer::instance().tokenize(context, padding);
        {
            llama_batch batch = llama_batch_get_one(ctx.data(), static_cast<int32_t>(ctx.size()));
            int32_t rc = llama_decode(llama_ctx.get(), batch);
            if (rc != 0) {
                throw std::runtime_error("llama_decode failed");
            }
        }
        for (auto &padd : padding) {
            std::vector<llama_token> candidate;
            std::map<llama_token, char32_t> inv_mapping;
            for (auto &c : padd.get_candidates()) {
                auto token = Tokenizer::instance().map_char(c);
                if (token != -1) {
                    candidate.push_back(token);
                    inv_mapping[token] = c;
                }
            }
            auto prob_res = masked_predict(candidate);
            llama_token chosen = prob_res.front().token;

            llama_batch batch = llama_batch_get_one(&chosen, 1);
            int32_t rc = llama_decode(llama_ctx.get(), batch);
            if (rc != 0) {
                throw std::runtime_error("llama_decode append failed");
            }

            std::vector<char32_t> new_candidate;
            for (auto &[token, prob] : prob_res) {
                new_candidate.push_back(inv_mapping[token]);
            }
            // debug print candidate and prob
            std::string debug_str;
            int count = 0;
            for (auto &[token, prob] : prob_res) {
                std::string s;
                utf8::append(inv_mapping[token], s);
                debug_str += s + " : " + std::to_string(prob) + ", ";
                if (++count >= 5) {
                    break;
                }
            }
            DebugSink::instance().send(L"INFO", debug_str);
            padd.set_candaiates(new_candidate);
            padd.predicted = true;
        }
    }

private:
    struct Token_Prob {
        llama_token token;
        float prob;
    };
    std::vector<Token_Prob> masked_predict(const std::vector<llama_token> &candidate) {
        float *logits = llama_get_logits_ith(llama_ctx.get(), -1);  // 最後一個 token 的 logits
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
};
}  // namespace tsf
