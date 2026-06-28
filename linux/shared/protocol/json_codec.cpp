#include "protocol/json_codec.hpp"

#include "text/utf.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace ime::linux {

namespace {

nlohmann::json padding_to_json(const PaddingEntry& entry) {
    if (entry.chosen && entry.chosen_char == 0) throw std::runtime_error("chosen padding requires chosen_char");
    return nlohmann::json{{"chosen", entry.chosen},
                          {"bopomofo", u16_to_utf8(entry.bopomofo)},
                          {"chosen_char", entry.chosen_char == 0 ? std::string{} : char32_to_utf8(entry.chosen_char)}};
}

std::string required_char32_to_utf8(char32_t value, const char* field) {
    if (value == 0) throw std::runtime_error(std::string("protocol field requires a nonzero codepoint: ") + field);
    return char32_to_utf8(value);
}

const nlohmann::json& required_field(const nlohmann::json& json, const char* name) {
    if (!json.is_object() || !json.contains(name)) throw std::runtime_error(std::string("missing protocol field: ") + name);
    return json.at(name);
}

void require_type(const nlohmann::json& json, const char* expected) {
    const auto& type = required_field(json, "type");
    if (!type.is_string() || type.get<std::string>() != expected) {
        throw std::runtime_error(std::string("unexpected protocol message type, expected ") + expected);
    }
}

std::string required_string(const nlohmann::json& json, const char* name) {
    const auto& value = required_field(json, name);
    if (!value.is_string()) throw std::runtime_error(std::string("protocol field must be a string: ") + name);
    return value.get<std::string>();
}

bool required_bool(const nlohmann::json& json, const char* name) {
    const auto& value = required_field(json, name);
    if (!value.is_boolean()) throw std::runtime_error(std::string("protocol field must be a boolean: ") + name);
    return value.get<bool>();
}

PaddingEntry padding_from_json(const nlohmann::json& json) {
    PaddingEntry entry;
    entry.chosen = required_bool(json, "chosen");
    entry.bopomofo = utf8_to_u16(required_string(json, "bopomofo"));
    const auto chosen = required_string(json, "chosen_char");
    if (chosen.empty()) {
        if (entry.chosen) throw std::runtime_error("chosen padding requires chosen_char");
    } else {
        const auto decoded = utf8_to_u32(chosen);
        if (decoded.size() != 1) throw std::runtime_error("chosen_char must contain exactly one codepoint");
        entry.chosen_char = decoded.front();
    }
    return entry;
}

nlohmann::json to_json_payload(const PredictRequest& request) {
    nlohmann::json padding = nlohmann::json::array();
    for (const auto& entry : request.padding) padding.push_back(padding_to_json(entry));
    return nlohmann::json{{"type", "predict_request"}, {"context", u16_to_utf8(request.context)}, {"padding", padding}};
}

nlohmann::json to_json_payload(const PredictResponse& response) {
    nlohmann::json candidates = nlohmann::json::array();
    for (const auto& list : response.candidates) {
        nlohmann::json encoded_list = nlohmann::json::array();
        for (char32_t candidate : list) encoded_list.push_back(required_char32_to_utf8(candidate, "candidates"));
        candidates.push_back(std::move(encoded_list));
    }
    return nlohmann::json{{"type", "predict_response"}, {"candidates", candidates}};
}

nlohmann::json to_json_payload(const StatusResponse& response) {
    return nlohmann::json{{"type", "status_response"},
                          {"running", response.running},
                          {"model_loaded", response.model_loaded},
                          {"backend", response.backend},
                          {"model_path", response.model_path},
                          {"error", response.error}};
}

nlohmann::json to_json_payload(const ControlRequest& request) {
    return nlohmann::json{{"type", "control_request"}, {"operation", request.operation}};
}

ByteVector encode_payload(const nlohmann::json& payload) {
    const std::string text = payload.dump();
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("protocol payload too large");
    }

    const auto length = static_cast<std::uint32_t>(text.size());
    ByteVector bytes;
    bytes.reserve(4 + text.size());
    bytes.push_back(static_cast<std::uint8_t>(length & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((length >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((length >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((length >> 24U) & 0xFFU));
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

nlohmann::json decode_payload(const ByteVector& bytes) {
    if (bytes.size() < 4) throw std::runtime_error("protocol message missing length prefix");
    const auto length = static_cast<std::uint32_t>(bytes[0]) |
                        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    if (bytes.size() != 4ULL + length) throw std::runtime_error("protocol message length mismatch");
    return nlohmann::json::parse(bytes.begin() + 4, bytes.end());
}

PredictRequest predict_request_from_payload(const nlohmann::json& json) {
    require_type(json, "predict_request");
    PredictRequest request;
    request.context = utf8_to_u16(required_string(json, "context"));
    const auto& padding = required_field(json, "padding");
    if (!padding.is_array()) throw std::runtime_error("protocol field must be an array: padding");
    for (const auto& entry : padding) request.padding.push_back(padding_from_json(entry));
    return request;
}

PredictResponse predict_response_from_payload(const nlohmann::json& json) {
    require_type(json, "predict_response");
    PredictResponse response;
    const auto& candidates = required_field(json, "candidates");
    if (!candidates.is_array()) throw std::runtime_error("protocol field must be an array: candidates");
    for (const auto& list : candidates) {
        if (!list.is_array()) throw std::runtime_error("candidate list must be an array");
        std::vector<char32_t> decoded;
        for (const auto& item : list) {
            const auto text = item.get<std::string>();
            const auto codepoints = utf8_to_u32(text);
            if (codepoints.size() != 1) throw std::runtime_error("candidate must contain exactly one codepoint");
            if (codepoints.front() == 0) throw std::runtime_error("candidate must contain a nonzero codepoint");
            decoded.push_back(codepoints.front());
        }
        response.candidates.push_back(std::move(decoded));
    }
    return response;
}

StatusResponse status_response_from_payload(const nlohmann::json& json) {
    require_type(json, "status_response");
    StatusResponse response;
    response.running = required_bool(json, "running");
    response.model_loaded = required_bool(json, "model_loaded");
    response.backend = required_string(json, "backend");
    response.model_path = required_string(json, "model_path");
    response.error = required_string(json, "error");
    return response;
}

ControlRequest control_request_from_payload(const nlohmann::json& json) {
    require_type(json, "control_request");
    ControlRequest request;
    request.operation = required_string(json, "operation");
    if (request.operation != "status" && request.operation != "stop" && request.operation != "reload_config") {
        throw std::runtime_error("unknown control operation: " + request.operation);
    }
    return request;
}

}  // namespace

ByteVector encode_message(const PredictRequest& request) {
    return encode_payload(to_json_payload(request));
}

ByteVector encode_message(const PredictResponse& response) {
    return encode_payload(to_json_payload(response));
}

ByteVector encode_message(const StatusResponse& response) {
    return encode_payload(to_json_payload(response));
}

ByteVector encode_message(const ControlRequest& request) {
    return encode_payload(to_json_payload(request));
}

PredictRequest decode_predict_request(const ByteVector& bytes) {
    return predict_request_from_payload(decode_payload(bytes));
}

PredictResponse decode_predict_response(const ByteVector& bytes) {
    return predict_response_from_payload(decode_payload(bytes));
}

StatusResponse decode_status_response(const ByteVector& bytes) {
    return status_response_from_payload(decode_payload(bytes));
}

ControlRequest decode_control_request(const ByteVector& bytes) {
    return control_request_from_payload(decode_payload(bytes));
}

std::string decode_message_type(const ByteVector& bytes) {
    const auto json = decode_payload(bytes);
    return required_string(json, "type");
}

}  // namespace ime::linux
