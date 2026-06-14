#pragma once

#include <windows.h>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/bopomofo.hpp"
#include "../utils/paths.hpp"
#include "engine.h"

namespace imesvc {

struct OnnxTensorData {
    std::vector<float> data;
    std::vector<int64_t> shape;
};

class OnnxModelManager {
    struct SessionBundle {
        Ort::Session session{nullptr};
        std::vector<std::string> input_names;
        std::vector<std::string> output_names;
    };

    Ort::Env _env;
    Ort::AllocatorWithDefaultOptions _allocator;
    SessionBundle _prefill;
    SessionBundle _decode;
    std::vector<std::string> _kv_output_names;
    std::unordered_map<std::string, size_t> _decode_past_input_to_cache_index;

    static std::filesystem::path resolve_prefill_path(std::source_location loc = std::source_location::current()) {
        return project_root(loc) / "models" / "onnx" / "ime_llama_prefill_xint8.onnx";
    }

    static std::filesystem::path resolve_decode_path(std::source_location loc = std::source_location::current()) {
        return project_root(loc) / "models" / "onnx" / "ime_llama_decode_xint8.onnx";
    }

    static bool starts_with(std::string_view text, std::string_view prefix) {
        return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    static std::optional<std::string> read_env_string(const char* name) {
        char buffer[4096]{};
        const DWORD len = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (len == 0 || len >= sizeof(buffer)) {
            return std::nullopt;
        }
        return std::string(buffer, len);
    }

    static void append_option(std::vector<std::string>& keys, std::vector<std::string>& values, const char* key,
                              const std::optional<std::string>& value) {
        if (!value || value->empty()) return;
        keys.emplace_back(key);
        values.push_back(*value);
    }

    static void append_vitisai(Ort::SessionOptions& options, std::string_view cache_key) {
        std::vector<std::string> keys;
        std::vector<std::string> values;
        append_option(keys, values, "cache_dir", read_env_string("IME_VITISAI_CACHE_DIR"));
        append_option(keys, values, "xclbin", read_env_string("IME_VITISAI_XCLBIN"));
        append_option(keys, values, "config_file", read_env_string("IME_VITISAI_CONFIG_FILE"));

        keys.emplace_back("log_level");
        values.push_back(read_env_string("IME_VITISAI_LOG_LEVEL").value_or("info"));

        keys.emplace_back("cache_key");
        values.emplace_back(cache_key);

        std::vector<const char*> key_ptrs;
        std::vector<const char*> value_ptrs;
        key_ptrs.reserve(keys.size());
        value_ptrs.reserve(values.size());
        for (const auto& key : keys) key_ptrs.push_back(key.c_str());
        for (const auto& value : values) value_ptrs.push_back(value.c_str());

        OrtStatus* status = Ort::GetApi().SessionOptionsAppendExecutionProvider_VitisAI(
            options, key_ptrs.data(), value_ptrs.data(), key_ptrs.size());
        if (status == nullptr) {
            std::cerr << "[SRV] ONNX Runtime VitisAI provider enabled cache_key=" << cache_key << std::endl;
            return;
        }

        const OrtApi& api = Ort::GetApi();
        std::string message = api.GetErrorMessage(status);
        api.ReleaseStatus(status);
        throw std::runtime_error("failed to enable VitisAIExecutionProvider: " + message);
    }

    static Ort::SessionOptions make_session_options(std::string_view cache_key) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(8);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        append_vitisai(options, cache_key);
        return options;
    }

    static std::vector<std::string> read_names(Ort::Session& session, Ort::AllocatorWithDefaultOptions& allocator,
                                               bool inputs) {
        const size_t count = inputs ? session.GetInputCount() : session.GetOutputCount();
        std::vector<std::string> names;
        names.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto name = inputs ? session.GetInputNameAllocated(i, allocator)
                               : session.GetOutputNameAllocated(i, allocator);
            names.emplace_back(name.get());
        }
        return names;
    }

    static std::vector<const char*> name_ptrs(const std::vector<std::string>& names) {
        std::vector<const char*> ptrs;
        ptrs.reserve(names.size());
        for (const auto& name : names) {
            ptrs.push_back(name.c_str());
        }
        return ptrs;
    }

    void load_sessions() {
        auto prefill_options = make_session_options("ime_llama_prefill_xint8");
        auto decode_options = make_session_options("ime_llama_decode_xint8");
        const auto prefill_path = resolve_prefill_path();
        const auto decode_path = resolve_decode_path();

        std::cerr << "[SRV] loading ONNX prefill model: " << prefill_path.string() << std::endl;
        _prefill.session = Ort::Session(_env, prefill_path.wstring().c_str(), prefill_options);
        std::cerr << "[SRV] loading ONNX decode model: " << decode_path.string() << std::endl;
        _decode.session = Ort::Session(_env, decode_path.wstring().c_str(), decode_options);

        _prefill.input_names = read_names(_prefill.session, _allocator, true);
        _prefill.output_names = read_names(_prefill.session, _allocator, false);
        _decode.input_names = read_names(_decode.session, _allocator, true);
        _decode.output_names = read_names(_decode.session, _allocator, false);
    }

    void build_cache_name_map() {
        _kv_output_names.clear();
        _decode_past_input_to_cache_index.clear();

        for (size_t i = 1; i < _prefill.output_names.size(); ++i) {
            if (starts_with(_prefill.output_names[i], "present_")) {
                _kv_output_names.push_back(_prefill.output_names[i]);
            }
        }

        if (_kv_output_names.empty()) {
            throw std::runtime_error("ONNX prefill model did not expose present_key/value outputs");
        }

        std::unordered_map<std::string, size_t> present_index;
        for (size_t i = 0; i < _kv_output_names.size(); ++i) {
            present_index[_kv_output_names[i]] = i;
        }

        for (const auto& name : _decode.input_names) {
            if (!starts_with(name, "past_")) continue;
            const std::string present_name = "present_" + name.substr(std::string_view("past_").size());
            const auto it = present_index.find(present_name);
            if (it == present_index.end()) {
                throw std::runtime_error("decode past input has no matching prefill present output: " + name);
            }
            _decode_past_input_to_cache_index[name] = it->second;
        }

        if (_decode_past_input_to_cache_index.size() != _kv_output_names.size()) {
            throw std::runtime_error("ONNX decode model past inputs do not match prefill present outputs");
        }
    }

    static void log_names(std::string_view label, const std::vector<std::string>& names) {
        std::cerr << label;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i != 0) std::cerr << ",";
            std::cerr << names[i];
        }
        std::cerr << std::endl;
    }

public:
    OnnxModelManager() : _env(ORT_LOGGING_LEVEL_WARNING, "IME_Service") {
        load_sessions();

        if (_prefill.input_names.empty() || _prefill.output_names.empty() || _decode.input_names.empty() ||
            _decode.output_names.empty()) {
            throw std::runtime_error("ONNX prefill/decode models must have inputs and outputs");
        }

        build_cache_name_map();
        log_names("[SRV] ONNX prefill inputs=", _prefill.input_names);
        log_names("[SRV] ONNX prefill outputs=", _prefill.output_names);
        log_names("[SRV] ONNX decode inputs=", _decode.input_names);
        log_names("[SRV] ONNX decode outputs=", _decode.output_names);
        std::cerr << "[SRV] ONNX KV model loaded kv_tensors=" << _kv_output_names.size() << std::endl;
    }

    static void initialize() {
        (void)instance();
    }

    static OnnxModelManager& instance() {
        static OnnxModelManager manager;
        return manager;
    }

    Ort::Session& prefill_session() {
        return _prefill.session;
    }

    Ort::Session& decode_session() {
        return _decode.session;
    }

    std::vector<const char*> prefill_input_ptrs() const {
        return name_ptrs(_prefill.input_names);
    }

    std::vector<const char*> prefill_output_ptrs() const {
        return name_ptrs(_prefill.output_names);
    }

    std::vector<const char*> decode_input_ptrs() const {
        return name_ptrs(_decode.input_names);
    }

    std::vector<const char*> decode_output_ptrs() const {
        return name_ptrs(_decode.output_names);
    }

    const std::vector<std::string>& prefill_input_names() const {
        return _prefill.input_names;
    }

    const std::vector<std::string>& decode_input_names() const {
        return _decode.input_names;
    }

    size_t cache_index_for_decode_input(const std::string& name) const {
        const auto it = _decode_past_input_to_cache_index.find(name);
        if (it == _decode_past_input_to_cache_index.end()) {
            throw std::runtime_error("unknown decode past input: " + name);
        }
        return it->second;
    }

    size_t kv_tensor_count() const {
        return _kv_output_names.size();
    }
};

class OnnxEngine : public IEngine {
    std::vector<int64_t> prev_tokens;
    std::vector<float> logits;
    std::vector<OnnxTensorData> kv_cache;

    struct PredictTiming {
        long long tokenize_us = 0;
        long long cache_us = 0;
        long long prefill_us = 0;
        long long decode_us = 0;
        long long candidate_us = 0;
        long long mask_us = 0;
        size_t prefill_calls = 0;
        size_t decode_calls = 0;
        size_t cache_tokens = 0;
        size_t candidate_tokens = 0;
    };

public:
    OnnxEngine() {
        OnnxModelManager::initialize();
        std::cerr << "[SRV] ONNX KV engine ready" << std::endl;
    }

    std::vector<PredictResult> predict(const std::u16string& context,
                                       const std::vector<PaddingEntry>& padding) override {
        const auto predict_start = std::chrono::steady_clock::now();
        PredictTiming timing;

        const auto tokenize_start = std::chrono::steady_clock::now();
        auto& tok = Tokenizer::instance();
        std::vector<int> new_tokens = tok.tokenize(context, padding);
        timing.tokenize_us += elapsed_us(tokenize_start);

        const auto cache_start = std::chrono::steady_clock::now();
        ensure_cache_aligned(new_tokens, timing);
        timing.cache_us += elapsed_us(cache_start);

        std::vector<PredictResult> results;
        results.reserve(padding.size());

        for (const auto& entry : padding) {
            PredictResult result;

            if (!entry.is_chosen) {
                const auto candidate_start = std::chrono::steady_clock::now();
                auto candidates = HanziMapEngine::instance().lookup_all(entry.bpmf);
                if (!candidates.empty()) {
                    std::vector<int> cand_tokens;
                    std::map<int, char32_t> inv;
                    for (auto c : candidates) {
                        const int token = tok.map_char(c);
                        if (token != -1) {
                            cand_tokens.push_back(token);
                            inv[token] = c;
                        }
                    }
                    timing.candidate_us += elapsed_us(candidate_start);
                    timing.candidate_tokens += cand_tokens.size();

                    if (!cand_tokens.empty()) {
                        auto probs = masked_predict(cand_tokens, timing);
                        for (const auto& [token, prob] : probs) {
                            result.candidates.push_back({inv[token], prob});
                        }
                        decode_one(probs.front().token, timing);
                    }
                } else {
                    timing.candidate_us += elapsed_us(candidate_start);
                }
            } else {
                const auto candidate_start = std::chrono::steady_clock::now();
                const int token = tok.map_char(entry.chosen_char);
                timing.candidate_us += elapsed_us(candidate_start);
                if (token != -1) {
                    timing.candidate_tokens++;
                    decode_one(token, timing);
                }
            }

            results.push_back(std::move(result));
        }

        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - predict_start)
                .count();
        std::cout << "[TIME] predict_ms=" << elapsed_ms << std::endl;
        print_timing(timing);
        std::cerr << "[SRV] ONNX KV predict done" << std::endl;
        return results;
    }

private:
    struct TokenProb {
        int token;
        float prob;
    };

    static long long elapsed_us(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    }

    static double ms(long long us) {
        return static_cast<double>(us) / 1000.0;
    }

    static void print_timing(const PredictTiming& timing) {
        std::cout << std::fixed << std::setprecision(3) << "[TIME] onnx_kv_detail_ms"
                  << " tokenize=" << ms(timing.tokenize_us) << " cache_total=" << ms(timing.cache_us)
                  << " prefill=" << ms(timing.prefill_us) << " decode=" << ms(timing.decode_us)
                  << " candidate=" << ms(timing.candidate_us) << " mask=" << ms(timing.mask_us)
                  << " prefill_calls=" << timing.prefill_calls << " decode_calls=" << timing.decode_calls
                  << " cache_tokens=" << timing.cache_tokens
                  << " candidate_tokens=" << timing.candidate_tokens << std::defaultfloat << std::endl;
    }

    static OnnxTensorData copy_tensor(Ort::Value& value) {
        if (!value.IsTensor()) {
            throw std::runtime_error("ONNX output is not a tensor");
        }

        auto info = value.GetTensorTypeAndShapeInfo();
        OnnxTensorData tensor;
        tensor.shape = info.GetShape();
        const size_t elem_count = info.GetElementCount();
        const float* data = value.GetTensorData<float>();
        tensor.data.assign(data, data + elem_count);
        return tensor;
    }

    static std::vector<float> copy_logits(Ort::Value& value) {
        OnnxTensorData tensor = copy_tensor(value);
        if (tensor.shape.empty()) {
            throw std::runtime_error("ONNX logits output has invalid rank");
        }

        const size_t vocab_size = static_cast<size_t>(tensor.shape.back());
        if (vocab_size == 0 || tensor.data.size() < vocab_size) {
            throw std::runtime_error("ONNX logits output has invalid shape");
        }

        return std::vector<float>(tensor.data.end() - static_cast<std::ptrdiff_t>(vocab_size), tensor.data.end());
    }

    static Ort::Value make_i64_tensor(Ort::MemoryInfo& memory_info, std::vector<int64_t>& data,
                                      const std::vector<int64_t>& shape) {
        return Ort::Value::CreateTensor<int64_t>(memory_info, data.data(), data.size(), shape.data(), shape.size());
    }

    static Ort::Value make_f32_tensor(Ort::MemoryInfo& memory_info, OnnxTensorData& tensor) {
        return Ort::Value::CreateTensor<float>(memory_info, tensor.data.data(), tensor.data.size(),
                                               tensor.shape.data(), tensor.shape.size());
    }

    void clear_cache() {
        prev_tokens.clear();
        logits.clear();
        kv_cache.clear();
    }

    void run_prefill(const std::vector<int64_t>& tokens, PredictTiming& timing) {
        if (tokens.empty()) {
            clear_cache();
            return;
        }

        auto& manager = OnnxModelManager::instance();
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> input_ids = tokens;
        std::vector<int64_t> attention_mask(tokens.size(), int64_t{1});
        const std::vector<int64_t> shape{1, static_cast<int64_t>(tokens.size())};

        std::vector<Ort::Value> inputs;
        inputs.reserve(manager.prefill_input_names().size());
        for (const auto& name : manager.prefill_input_names()) {
            if (name == "input_ids") {
                inputs.push_back(make_i64_tensor(memory_info, input_ids, shape));
            } else if (name == "attention_mask") {
                inputs.push_back(make_i64_tensor(memory_info, attention_mask, shape));
            } else {
                throw std::runtime_error("unsupported ONNX prefill input: " + name);
            }
        }

        auto input_names = manager.prefill_input_ptrs();
        auto output_names = manager.prefill_output_ptrs();
        const auto start = std::chrono::steady_clock::now();
        auto outputs = manager.prefill_session().Run(Ort::RunOptions{nullptr}, input_names.data(), inputs.data(),
                                                     inputs.size(), output_names.data(), output_names.size());
        timing.prefill_us += elapsed_us(start);
        timing.prefill_calls++;

        if (outputs.size() != manager.kv_tensor_count() + 1) {
            throw std::runtime_error("ONNX prefill output count does not match KV cache count");
        }

        logits = copy_logits(outputs.front());
        kv_cache.clear();
        kv_cache.reserve(outputs.size() - 1);
        for (size_t i = 1; i < outputs.size(); ++i) {
            kv_cache.push_back(copy_tensor(outputs[i]));
        }
        prev_tokens = tokens;
    }

    void decode_one(int token, PredictTiming& timing) {
        if (kv_cache.empty() || prev_tokens.empty()) {
            std::vector<int64_t> tokens{static_cast<int64_t>(token)};
            run_prefill(tokens, timing);
            timing.cache_tokens++;
            return;
        }

        auto& manager = OnnxModelManager::instance();
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<int64_t> input_id{static_cast<int64_t>(token)};
        std::vector<int64_t> attention_mask(prev_tokens.size() + 1, int64_t{1});
        std::vector<int64_t> position_id{static_cast<int64_t>(prev_tokens.size())};
        const std::vector<int64_t> token_shape{1, 1};
        const std::vector<int64_t> mask_shape{1, static_cast<int64_t>(attention_mask.size())};

        std::vector<Ort::Value> inputs;
        inputs.reserve(manager.decode_input_names().size());
        for (const auto& name : manager.decode_input_names()) {
            if (name == "input_ids") {
                inputs.push_back(make_i64_tensor(memory_info, input_id, token_shape));
            } else if (name == "attention_mask") {
                inputs.push_back(make_i64_tensor(memory_info, attention_mask, mask_shape));
            } else if (name == "position_ids") {
                inputs.push_back(make_i64_tensor(memory_info, position_id, token_shape));
            } else {
                const size_t cache_index = manager.cache_index_for_decode_input(name);
                inputs.push_back(make_f32_tensor(memory_info, kv_cache[cache_index]));
            }
        }

        auto input_names = manager.decode_input_ptrs();
        auto output_names = manager.decode_output_ptrs();
        const auto start = std::chrono::steady_clock::now();
        auto outputs = manager.decode_session().Run(Ort::RunOptions{nullptr}, input_names.data(), inputs.data(),
                                                    inputs.size(), output_names.data(), output_names.size());
        timing.decode_us += elapsed_us(start);
        timing.decode_calls++;

        if (outputs.size() != manager.kv_tensor_count() + 1) {
            throw std::runtime_error("ONNX decode output count does not match KV cache count");
        }

        logits = copy_logits(outputs.front());
        std::vector<OnnxTensorData> next_cache;
        next_cache.reserve(outputs.size() - 1);
        for (size_t i = 1; i < outputs.size(); ++i) {
            next_cache.push_back(copy_tensor(outputs[i]));
        }
        kv_cache = std::move(next_cache);
        prev_tokens.push_back(static_cast<int64_t>(token));
        timing.cache_tokens++;
    }

    void ensure_cache_aligned(const std::vector<int>& new_tokens, PredictTiming& timing) {
        size_t common = 0;
        while (common < prev_tokens.size() && common < new_tokens.size() &&
               prev_tokens[common] == static_cast<int64_t>(new_tokens[common])) {
            common++;
        }

        if (common < prev_tokens.size()) {
            if (common == 0) {
                clear_cache();
            } else {
                std::vector<int64_t> prefix;
                prefix.reserve(common);
                for (size_t i = 0; i < common; ++i) {
                    prefix.push_back(static_cast<int64_t>(new_tokens[i]));
                }
                run_prefill(prefix, timing);
                timing.cache_tokens += common;
            }
        }

        if (prev_tokens.empty()) {
            std::vector<int64_t> tokens;
            tokens.reserve(new_tokens.size());
            for (int token : new_tokens) {
                tokens.push_back(static_cast<int64_t>(token));
            }
            run_prefill(tokens, timing);
            timing.cache_tokens += tokens.size();
            return;
        }

        for (size_t i = common; i < new_tokens.size(); ++i) {
            decode_one(new_tokens[i], timing);
        }
    }

    std::vector<TokenProb> masked_predict(const std::vector<int>& candidate, PredictTiming& timing) {
        const auto mask_start = std::chrono::steady_clock::now();
        std::vector<float> cand_logits;
        cand_logits.reserve(candidate.size());

        for (int token : candidate) {
            if (token < 0 || static_cast<size_t>(token) >= logits.size()) {
                throw std::runtime_error("candidate token is outside ONNX logits vocabulary");
            }
            cand_logits.push_back(logits[static_cast<size_t>(token)]);
        }

        float max_logit = *std::ranges::max_element(cand_logits);
        std::vector<float> exps(candidate.size());
        float sum = 0.0f;
        for (size_t i = 0; i < candidate.size(); ++i) {
            exps[i] = std::exp(cand_logits[i] - max_logit);
            sum += exps[i];
        }

        std::vector<TokenProb> probs;
        probs.reserve(candidate.size());
        for (size_t i = 0; i < candidate.size(); ++i) {
            probs.push_back({candidate[i], exps[i] / sum});
        }

        std::ranges::sort(probs, [](const TokenProb& a, const TokenProb& b) {
            return a.prob > b.prob;
        });
        timing.mask_us += elapsed_us(mask_start);
        return probs;
    }
};

}  // namespace imesvc
