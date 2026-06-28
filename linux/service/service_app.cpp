#include "service/service_app.hpp"

#include <cstdlib>
#include <utility>

namespace ime::linux {

namespace {

std::filesystem::path default_table_path() {
    if (const char* override = std::getenv("IME_LINUX_TABLE_PATH")) {
        if (override[0] != '\0') return override;
    }
#ifdef IME_LINUX_SOURCE_DIR
    return std::filesystem::path(IME_LINUX_SOURCE_DIR) / ".." / "tables" / "bopomofo_char.json";
#else
    return "/usr/share/ime/tables/bopomofo_char.json";
#endif
}

}  // namespace

ServiceApp::ServiceApp(Config config) : ServiceApp(std::move(config), default_table_path()) {}

ServiceApp::ServiceApp(Config config, std::filesystem::path table_path)
    : config_(std::move(config)), table_path_(std::move(table_path)), last_activity_(std::chrono::steady_clock::now()) {}

StatusResponse ServiceApp::handle_status() const {
    StatusResponse status;
    status.running = !stop_requested_;
    status.model_loaded = llama_.ready();
    status.backend = llama_.ready() ? "llama.cpp" : "fallback";
    status.model_path = config_.model_path;
    if (config_.model_path.empty()) {
        status.error = "model not configured";
    } else if (!llama_.ready()) {
        status.error = backend_error_.empty() ? "model not loaded" : backend_error_;
    }
    return status;
}

PredictResponse ServiceApp::handle_predict(const PredictRequest& request) {
    touch();
    if (!config_.model_path.empty()) {
        ensure_llama_loaded();
        if (llama_.ready()) {
            try {
                return llama_.predict(request);
            } catch (const std::exception& error) {
                backend_error_ = error.what();
            }
        }
    }

    if (!fallback_) fallback_.emplace(table_path_);

    PredictResponse response;
    response.candidates.reserve(request.padding.size());
    for (const auto& entry : request.padding) {
        if (entry.chosen && entry.chosen_char != 0) {
            response.candidates.push_back({entry.chosen_char});
        } else {
            CompositionBuffer buffer;
            for (char16_t symbol : entry.bopomofo) buffer.add_bopomofo(symbol);
            const auto predictions = fallback_->predict(buffer);
            if (predictions.empty()) {
                response.candidates.push_back({});
            } else {
                response.candidates.push_back(predictions.back().candidates);
            }
        }
    }
    return response;
}

void ServiceApp::handle_stop() noexcept {
    stop_requested_ = true;
}

bool ServiceApp::stop_requested() const noexcept {
    return stop_requested_;
}

bool ServiceApp::expired_idle_timeout(std::chrono::steady_clock::time_point now) const {
    return now - last_activity_ >= std::chrono::seconds(config_.idle_timeout_seconds);
}

void ServiceApp::touch() {
    last_activity_ = std::chrono::steady_clock::now();
}

void ServiceApp::ensure_llama_loaded() {
    if (attempted_llama_load_) return;
    attempted_llama_load_ = true;
    try {
        llama_.load(config_);
        backend_error_.clear();
    } catch (const std::exception& error) {
        backend_error_ = error.what();
    }
}

}  // namespace ime::linux
