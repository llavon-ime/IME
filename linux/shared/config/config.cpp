#include "config/config.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string_view>
#include <thread>
#include <utility>

namespace ime::linux {

namespace {

const char* non_empty_env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? value : nullptr;
}

bool allowed_value(std::string_view value, std::initializer_list<std::string_view> allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

std::string string_field(const nlohmann::json& json, const char* name, const std::string& fallback,
                         std::initializer_list<std::string_view> allowed = {}) {
    if (!json.is_object() || !json.contains(name) || !json.at(name).is_string()) return fallback;

    const auto value = json.at(name).get<std::string>();
    if (!allowed.size() || allowed_value(value, allowed)) return value;
    return fallback;
}

int int_field(const nlohmann::json& json, const char* name, int fallback, int minimum, int maximum) {
    if (!json.is_object() || !json.contains(name) || !json.at(name).is_number_integer()) return fallback;

    const int value = json.at(name).get<int>();
    if (value < minimum || value > maximum) return fallback;
    return value;
}

bool bool_field(const nlohmann::json& json, const char* name, bool fallback) {
    if (!json.is_object() || !json.contains(name) || !json.at(name).is_boolean()) return fallback;
    return json.at(name).get<bool>();
}

std::string trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value.erase(value.begin());
        value.pop_back();
    }
    return value;
}

std::map<std::string, std::string> read_simple_ini(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return {};

    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        const auto stripped = trim(line);
        if (stripped.empty() || stripped.front() == '#' || stripped.front() == '[') continue;

        const auto separator = stripped.find('=');
        if (separator == std::string::npos) continue;

        auto key = trim(std::string_view(stripped).substr(0, separator));
        auto value = trim(std::string_view(stripped).substr(separator + 1));
        if (!key.empty()) fields[std::move(key)] = unquote(std::move(value));
    }
    return fields;
}

std::string ini_string_field(const std::map<std::string, std::string>& fields, const char* name,
                             const std::string& fallback,
                             std::initializer_list<std::string_view> allowed = {}) {
    const auto it = fields.find(name);
    if (it == fields.end()) return fallback;
    if (!allowed.size() || allowed_value(it->second, allowed)) return it->second;
    return fallback;
}

int ini_int_field(const std::map<std::string, std::string>& fields, const char* name, int fallback, int minimum,
                  int maximum) {
    const auto it = fields.find(name);
    if (it == fields.end()) return fallback;

    try {
        size_t parsed = 0;
        const int value = std::stoi(it->second, &parsed);
        if (parsed != it->second.size() || value < minimum || value > maximum) return fallback;
        return value;
    } catch (...) {
        return fallback;
    }
}

bool ini_bool_field(const std::map<std::string, std::string>& fields, const char* name, bool fallback) {
    const auto it = fields.find(name);
    if (it == fields.end()) return fallback;

    if (it->second == "True" || it->second == "true" || it->second == "1") return true;
    if (it->second == "False" || it->second == "false" || it->second == "0") return false;
    return fallback;
}

std::string keyboard_layout_from_config(std::string_view value, const std::string& fallback) {
    if (value == "standard" || value == "標準") return "standard";
    return fallback;
}

std::string selection_keys_from_config(std::string_view value, const std::string& fallback) {
    if (value == "123456789" || value == "數字鍵") return "123456789";
    if (value == "asdfghjkl" || value == "本位列") return "asdfghjkl";
    if (value == "asdfzxcvb" || value == "左手鍵") return "asdfzxcvb";
    return fallback;
}

std::string candidate_layout_from_config(std::string_view value, const std::string& fallback) {
    if (value == "NotSet" || value == "not_set") return "not_set";
    if (value == "Vertical" || value == "vertical") return "vertical";
    if (value == "Horizontal" || value == "horizontal") return "horizontal";
    if (value == "系統預設") return "not_set";
    if (value == "垂直") return "vertical";
    if (value == "水平") return "horizontal";
    return fallback;
}

std::string select_phrase_from_config(std::string_view value, const std::string& fallback) {
    if (value == "before_cursor" || value == "游標前") return "before_cursor";
    if (value == "after_cursor" || value == "游標後") return "after_cursor";
    return fallback;
}

Config config_from_fcitx_ini(const std::filesystem::path& path) {
    Config cfg = default_config();
    const auto fields = read_simple_ini(path);
    if (fields.empty()) return cfg;

    cfg.model_path = ini_string_field(fields, "ModelPath", cfg.model_path);
    cfg.context_length = ini_int_field(fields, "ContextLength", cfg.context_length, 1, 1048576);
    cfg.thread_count = ini_int_field(fields, "ThreadCount", cfg.thread_count, 1, 1024);
    cfg.gpu_layers = ini_int_field(fields, "GpuLayers", cfg.gpu_layers, 0, 1024);
    cfg.idle_timeout_seconds = ini_int_field(fields, "IdleTimeoutSeconds", cfg.idle_timeout_seconds, 0, 86400);
    if (const auto it = fields.find("BopomofoKeyboardLayout"); it != fields.end()) {
        cfg.keyboard_layout = keyboard_layout_from_config(it->second, cfg.keyboard_layout);
    }
    if (const auto it = fields.find("SelectionKeys"); it != fields.end()) {
        cfg.selection_keys = selection_keys_from_config(it->second, cfg.selection_keys);
    }
    cfg.selection_key_count = ini_int_field(fields, "SelectionKeysCount", cfg.selection_key_count, 4, 9);
    cfg.candidate_page_size = ini_int_field(fields, "CandidatePageSize", cfg.candidate_page_size, 1, 50);
    if (const auto it = fields.find("CandidateLayout"); it != fields.end()) {
        cfg.candidate_layout = candidate_layout_from_config(it->second, cfg.candidate_layout);
    }
    cfg.space_selects_candidate = ini_bool_field(fields, "ChooseCandidateUsingSpace", cfg.space_selects_candidate);
    if (const auto it = fields.find("SelectPhrase"); it != fields.end()) {
        cfg.select_phrase = select_phrase_from_config(it->second, cfg.select_phrase);
    }
    cfg.move_cursor_after_selection =
        ini_bool_field(fields, "MoveCursorAfterSelection", cfg.move_cursor_after_selection);
    cfg.esc_clears_entire_buffer =
        ini_bool_field(fields, "EscKeyClearsEntireComposingBuffer", cfg.esc_clears_entire_buffer);
    cfg.caps_lock_inputs_bopomofo = ini_bool_field(fields, "CapsLockInputsBopomofo", cfg.caps_lock_inputs_bopomofo);
    return cfg;
}

Config load_legacy_json_config(const std::filesystem::path& path) {
    try {
        std::ifstream input(path);
        if (!input) return default_config();
        nlohmann::json json;
        input >> json;
        return config_from_json(json);
    } catch (...) {
        return default_config();
    }
}

}  // namespace

Config default_config() {
    Config cfg;
    const auto threads = std::thread::hardware_concurrency();
    cfg.thread_count = threads == 0 ? 1 : static_cast<int>(threads);
    return cfg;
}

Config load_config() {
    const auto path = config_path();
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) return config_from_fcitx_ini(path);

    const auto legacy_path = legacy_config_path();
    if (std::filesystem::exists(legacy_path, ec) && !ec) return load_legacy_json_config(legacy_path);
    return default_config();
}

nlohmann::json to_json(const Config& cfg) {
    return nlohmann::json{
        {"model_path", cfg.model_path},
        {"context_length", cfg.context_length},
        {"thread_count", cfg.thread_count},
        {"gpu_layers", cfg.gpu_layers},
        {"idle_timeout_seconds", cfg.idle_timeout_seconds},
        {"keyboard_layout", cfg.keyboard_layout},
        {"selection_keys", cfg.selection_keys},
        {"selection_key_count", cfg.selection_key_count},
        {"candidate_page_size", cfg.candidate_page_size},
        {"candidate_layout", cfg.candidate_layout},
        {"space_selects_candidate", cfg.space_selects_candidate},
        {"select_phrase", cfg.select_phrase},
        {"move_cursor_after_selection", cfg.move_cursor_after_selection},
        {"esc_clears_entire_buffer", cfg.esc_clears_entire_buffer},
        {"caps_lock_inputs_bopomofo", cfg.caps_lock_inputs_bopomofo},
    };
}

Config config_from_json(const nlohmann::json& json) {
    Config cfg = default_config();
    if (!json.is_object()) return cfg;

    cfg.model_path = string_field(json, "model_path", cfg.model_path);
    cfg.context_length = int_field(json, "context_length", cfg.context_length, 1, 1048576);
    cfg.thread_count = int_field(json, "thread_count", cfg.thread_count, 1, 1024);
    cfg.gpu_layers = int_field(json, "gpu_layers", cfg.gpu_layers, 0, 1024);
    cfg.idle_timeout_seconds = int_field(json, "idle_timeout_seconds", cfg.idle_timeout_seconds, 0, 86400);
    cfg.keyboard_layout = keyboard_layout_from_config(string_field(json, "keyboard_layout", cfg.keyboard_layout),
                                                      cfg.keyboard_layout);
    cfg.selection_keys = selection_keys_from_config(string_field(json, "selection_keys", cfg.selection_keys),
                                                    cfg.selection_keys);
    cfg.selection_key_count = int_field(json, "selection_key_count", cfg.selection_key_count, 4, 9);
    cfg.candidate_page_size = int_field(json, "candidate_page_size", cfg.candidate_page_size, 1, 50);
    cfg.candidate_layout = candidate_layout_from_config(string_field(json, "candidate_layout", cfg.candidate_layout),
                                                        cfg.candidate_layout);
    cfg.space_selects_candidate = bool_field(json, "space_selects_candidate", cfg.space_selects_candidate);
    cfg.select_phrase = select_phrase_from_config(string_field(json, "select_phrase", cfg.select_phrase), cfg.select_phrase);
    cfg.move_cursor_after_selection = bool_field(json, "move_cursor_after_selection", cfg.move_cursor_after_selection);
    cfg.esc_clears_entire_buffer = bool_field(json, "esc_clears_entire_buffer", cfg.esc_clears_entire_buffer);
    cfg.caps_lock_inputs_bopomofo = bool_field(json, "caps_lock_inputs_bopomofo", cfg.caps_lock_inputs_bopomofo);
    return cfg;
}

std::filesystem::path config_path() {
    if (const char* xdg = non_empty_env("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / "fcitx5" / "conf" / "ime-linux.conf";
    }
    if (const char* home = non_empty_env("HOME")) {
        return std::filesystem::path(home) / ".config" / "fcitx5" / "conf" / "ime-linux.conf";
    }
    return std::filesystem::path(".") / "fcitx5" / "conf" / "ime-linux.conf";
}

std::filesystem::path legacy_config_path() {
    if (const char* xdg = non_empty_env("XDG_CONFIG_HOME")) {
        return std::filesystem::path(xdg) / "ime-linux" / "config.json";
    }
    if (const char* home = non_empty_env("HOME")) {
        return std::filesystem::path(home) / ".config" / "ime-linux" / "config.json";
    }
    return std::filesystem::path(".") / "ime-linux" / "config.json";
}

std::filesystem::path runtime_dir() {
    if (const char* xdg = non_empty_env("XDG_RUNTIME_DIR")) {
        return std::filesystem::path(xdg) / "ime-linux";
    }
    return std::filesystem::temp_directory_path() / "ime-linux";
}

std::filesystem::path socket_path() {
    return runtime_dir() / "ime.sock";
}

std::filesystem::path pid_path() {
    return runtime_dir() / "service.pid";
}

}  // namespace ime::linux
