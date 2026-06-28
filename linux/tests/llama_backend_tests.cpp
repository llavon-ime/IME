#include "service/llama_backend.hpp"

#include "config/config.hpp"
#include "service/token_filter.hpp"

#include <cstdlib>
#include <string>
#include <vector>

int run_llama_backend_tests() {
    bool ok = true;

    ime::linux::LlamaBackend backend;
    ok = ok && !backend.ready();

    constexpr int unk = 4;
    std::vector<int> mixed_context = {unk, unk, 42, unk};
    ok = ok && ime::linux::remove_leading_unknown_context_tokens(mixed_context, unk) == 2;
    ok = ok && mixed_context == std::vector<int>({42, unk});

    std::vector<int> known_first_context = {42, unk};
    ok = ok && ime::linux::remove_leading_unknown_context_tokens(known_first_context, unk) == 0;
    ok = ok && known_first_context == std::vector<int>({42, unk});

    std::vector<int> only_unknown_context = {unk, unk};
    ok = ok && ime::linux::remove_leading_unknown_context_tokens(only_unknown_context, unk) == 2;
    ok = ok && only_unknown_context.empty();

    auto cfg = ime::linux::default_config();
    cfg.model_path = "/tmp/ime-linux-missing-model.gguf";
    bool load_error_reported = false;
    try {
        backend.load(cfg);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        load_error_reported = message.find(cfg.model_path) != std::string::npos &&
                              (message.find("unavailable") != std::string::npos ||
                               message.find("not found") != std::string::npos);
    }
    ok = ok && load_error_reported;

    if (const char* model_path = std::getenv("IME_LINUX_TEST_MODEL")) {
        auto model_cfg = ime::linux::default_config();
        model_cfg.model_path = model_path;
        model_cfg.context_length = 128;
        model_cfg.thread_count = 2;

        ime::linux::LlamaBackend model_backend;
        model_backend.load(model_cfg);
        ok = ok && model_backend.ready();

        ime::linux::PredictRequest request;
        request.padding.push_back({false, u"ㄋㄧˇ", 0});
        const auto response = model_backend.predict(request);
        ok = ok && response.candidates.size() == 1;
        ok = ok && !response.candidates.front().empty();
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
