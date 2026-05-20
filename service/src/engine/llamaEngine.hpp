#pragma once

#include <llama-cpp.h>
#include <utf8/cpp20.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

#include "../core/bopomofo.hpp"
#include "../utils/paths.hpp"
#include "engine.h"

namespace imesvc {

class ModelManager {
    static std::filesystem::path resolve_model_path(std::source_location loc = std::source_location::current()) {
        return project_root(loc) / "models" / "bopomofo-ime-llama-250m-Q4_K_M.gguf";
    }

    llama_model_ptr _model;
    const llama_vocab* _vocab;

    ModelManager() {
        llama_backend_init();
        auto path = resolve_model_path().string();
        std::cerr << "[SRV] loading model: " << path << std::endl;
        _model.reset(llama_model_load_from_file(path.c_str(), llama_model_default_params()));
        if (!_model) throw std::runtime_error("Failed to load model: " + path);
        _vocab = llama_model_get_vocab(_model.get());
        std::cerr << "[SRV] model loaded" << std::endl;
    }

public:
    static void initialize() { (void)instance(); }
    static ModelManager& instance() {
        static ModelManager e;
        return e;
    }
    llama_model* model() { return _model.get(); }
    const llama_vocab* vocab() { return _vocab; }
    llama_context* new_context() {
        std::cerr << "[SRV] creating context" << std::endl;
        auto ctx = llama_init_from_model(_model.get(), llama_context_default_params());
        if (!ctx) throw std::runtime_error("Failed to create llama context");
        std::cerr << "[SRV] context created" << std::endl;
        return ctx;
    }
};

class LlamaEngine : public IEngine {
    llama_context_ptr llama_ctx;
    std::vector<llama_token> prev_tokens;
    llama_memory_t mem;
    llama_pos next_pos = 0;

public:
    LlamaEngine() {
        ModelManager::initialize();
        llama_ctx.reset(ModelManager::instance().new_context());
        mem = llama_get_memory(llama_ctx.get());
        llama_memory_clear(mem, true);
        std::cerr << "[SRV] engine ready" << std::endl;
    }

    std::vector<PredictResult> predict(const std::u16string& context,
                                       const std::vector<PaddingEntry>& padding) override {
        const auto predict_start = std::chrono::steady_clock::now();
        auto& tok = Tokenizer::instance();

        std::vector<int> new_tokens = tok.tokenize(context, padding);

        debug_request(context, padding, new_tokens);

        std::cerr << "[SRV] predict: ctx_len=" << context.size()
                  << " pad_cnt=" << padding.size()
                  << " tokens=" << new_tokens.size() << std::endl;

        ensure_cache_aligned(new_tokens);

        std::vector<PredictResult> results;
        results.reserve(padding.size());

        for (size_t pi = 0; pi < padding.size(); pi++) {
            auto& entry = padding[pi];
            PredictResult r;
            if (!entry.is_chosen) {
                auto candidates = HanziMapEngine::instance().lookup_all(entry.bpmf);
                if (!candidates.empty()) {
                    std::vector<llama_token> cand_tokens;
                    std::map<llama_token, char32_t> inv;
                    for (auto c : candidates) {
                        auto t = tok.map_char(c);
                        if (t != -1) {
                            cand_tokens.push_back(t);
                            inv[t] = c;
                        }
                    }
                    if (!cand_tokens.empty()) {
                        auto probs = masked_predict(cand_tokens);
                        for (auto& [token, prob] : probs) r.candidates.push_back({inv[token], prob});
                        debug_top5(pi, entry, probs, inv);

                        llama_token best = static_cast<llama_token>(probs.front().token);
                        decode_one(best);
                    }
                } else {
                    debug_no_candidates(pi, entry);
                }
            } else {
                auto t = tok.map_char(entry.chosen_char);
                debug_chosen(pi, entry, t);
                if (t != -1) decode_one(t);
            }
            results.push_back(std::move(r));
        }

        const auto predict_end = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(predict_end - predict_start).count();
        std::cout << "[TIME] predict_ms=" << elapsed_ms << std::endl;
        std::cerr << "[SRV] predict done" << std::endl;
        return results;
    }

private:
    struct TokenProb {
        int token;
        float prob;
    };

    static std::string to_utf8(const std::u16string& text) {
        return utf8::utf16to8(text);
    }

    static std::string to_utf8(char32_t ch) {
        std::string text;
        utf8::append(ch, text);
        return text;
    }

    static std::string describe_padding(const PaddingEntry& entry) {
        if (entry.is_chosen) {
            return to_utf8(entry.chosen_char);
        }
        return "<" + to_utf8(entry.bpmf) + ">";
    }

    static void debug_request(const std::u16string& context,
                              const std::vector<PaddingEntry>& padding,
                              const std::vector<int>& tokens) {
        std::cout << "[REQ] context=\"" << to_utf8(context) << "\" padding=\"";
        for (const auto& entry : padding) {
            std::cout << describe_padding(entry);
        }

        std::cout << "\" tokens=[";
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (i != 0) std::cout << ' ';
            std::cout << tokens[i];
        }
        std::cout << "]" << std::endl;
    }

    static void debug_top5(size_t pos,
                           const PaddingEntry& entry,
                           const std::vector<TokenProb>& probs,
                           const std::map<llama_token, char32_t>& inv) {
        std::cout << "[POS " << pos << "] bpmf=\"" << to_utf8(entry.bpmf) << "\" top5=";
        const size_t count = std::min<size_t>(5, probs.size());
        for (size_t i = 0; i < count; ++i) {
            const auto token = static_cast<llama_token>(probs[i].token);
            const auto it = inv.find(token);
            const std::string word = (it == inv.end()) ? "?" : to_utf8(it->second);
            if (i != 0) std::cout << ", ";
            std::cout << word << "(token=" << token << ", p=" << std::fixed << std::setprecision(6)
                      << probs[i].prob << ")";
        }
        std::cout << std::defaultfloat << std::endl;
    }

    static void debug_no_candidates(size_t pos, const PaddingEntry& entry) {
        std::cout << "[POS " << pos << "] bpmf=\"" << to_utf8(entry.bpmf)
                  << "\" top5=<no candidates>" << std::endl;
    }

    static void debug_chosen(size_t pos, const PaddingEntry& entry, int token) {
        std::cout << "[POS " << pos << "] chosen=\"" << to_utf8(entry.chosen_char)
                  << "\" token=" << token << std::endl;
    }

    static llama_batch make_token_batch(const llama_token* tokens, size_t count, llama_pos start_pos, bool logits_last) {
        if (count == 0 || count > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
            throw std::runtime_error("invalid llama batch size");
        }

        llama_batch batch = llama_batch_init(static_cast<int32_t>(count), 0, 1);
        if (!batch.token || !batch.pos || !batch.n_seq_id || !batch.seq_id || !batch.logits) {
            llama_batch_free(batch);
            throw std::runtime_error("llama_batch_init failed");
        }

        batch.n_tokens = static_cast<int32_t>(count);
        for (int32_t i = 0; i < batch.n_tokens; ++i) {
            batch.token[i] = tokens[i];
            batch.pos[i] = start_pos + i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = (!logits_last || i == batch.n_tokens - 1) ? 1 : 0;
        }
        return batch;
    }

    void decode_one(llama_token token) {
        llama_batch batch = make_token_batch(&token, 1, next_pos, true);
        int rc = llama_decode(llama_ctx.get(), batch);
        llama_batch_free(batch);
        if (rc != 0) throw std::runtime_error("llama_decode failed in decode_one");

        ++next_pos;
        prev_tokens.push_back(token);
    }

    void ensure_cache_aligned(const std::vector<int>& new_tokens) {
        size_t common = 0;
        while (common < prev_tokens.size() && common < new_tokens.size() && prev_tokens[common] == new_tokens[common]) {
            common++;
        }

        std::cerr << "[SRV] cache: prev=" << prev_tokens.size()
                  << " new=" << new_tokens.size()
                  << " common=" << common << std::endl;

        if (common < prev_tokens.size()) {
            bool ok = llama_memory_seq_rm(mem, 0, static_cast<llama_pos>(common), -1);
            std::cerr << "[SRV] seq_rm from " << common << " -> " << (ok ? "ok" : "FAIL") << std::endl;
            prev_tokens.resize(common);
        }
        next_pos = static_cast<llama_pos>(common);

        size_t new_count = new_tokens.size() - common;
        if (new_count > 0) {
            std::vector<llama_token> prompt_tokens;
            prompt_tokens.reserve(new_count);
            for (size_t i = 0; i < new_count; ++i) {
                prompt_tokens.push_back(static_cast<llama_token>(new_tokens[common + i]));
            }

            llama_batch batch = make_token_batch(prompt_tokens.data(), prompt_tokens.size(), next_pos, true);
            int rc = llama_decode(llama_ctx.get(), batch);
            llama_batch_free(batch);
            if (rc != 0) throw std::runtime_error("llama_decode failed in ensure_cache");

            next_pos += static_cast<llama_pos>(new_count);
            prev_tokens.insert(prev_tokens.end(), new_tokens.begin() + common, new_tokens.end());
        }
    }

    std::vector<TokenProb> masked_predict(const std::vector<llama_token>& candidate) {
        float* logits = llama_get_logits_ith(llama_ctx.get(), -1);
        if (!logits) throw std::runtime_error("llama_get_logits_ith error");

        std::vector<float> cand_logits(candidate.size());
        for (size_t i = 0; i < candidate.size(); i++) cand_logits[i] = logits[candidate[i]];

        float max_logit = *std::ranges::max_element(cand_logits);
        std::vector<float> exps(candidate.size());
        float sum = 0.0f;
        for (size_t i = 0; i < candidate.size(); ++i) {
            exps[i] = std::exp(cand_logits[i] - max_logit);
            sum += exps[i];
        }

        std::vector<TokenProb> probs;
        probs.reserve(candidate.size());
        for (size_t i = 0; i < candidate.size(); ++i) probs.push_back({static_cast<int>(candidate[i]), exps[i] / sum});

        std::sort(probs.begin(), probs.end(), [](const TokenProb& a, const TokenProb& b) { return a.prob > b.prob; });
        return probs;
    }
};

}  // namespace imesvc
