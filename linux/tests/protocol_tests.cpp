#include "protocol/json_codec.hpp"
#include "protocol/protocol.hpp"
#include "ipc/unix_socket.hpp"
#include "text/utf.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

int run_protocol_tests() {
    bool ok = true;

    const auto frame = [](const std::string& text) {
        ime::linux::ByteVector bytes{static_cast<std::uint8_t>(text.size() & 0xFFU),
                                    static_cast<std::uint8_t>((text.size() >> 8U) & 0xFFU),
                                    static_cast<std::uint8_t>((text.size() >> 16U) & 0xFFU),
                                    static_cast<std::uint8_t>((text.size() >> 24U) & 0xFFU)};
        bytes.insert(bytes.end(), text.begin(), text.end());
        return bytes;
    };

    ime::linux::PredictRequest req;
    req.context = u"你好";
    req.padding.push_back({false, u"ㄋㄧˇ", 0});
    const auto bytes = ime::linux::encode_message(req);
    const auto decoded = ime::linux::decode_predict_request(bytes);
    ok = ok && decoded.context == req.context;
    ok = ok && decoded.padding.size() == 1;
    ok = ok && decoded.padding[0].bopomofo == u"ㄋㄧˇ";

    ime::linux::StatusResponse status;
    status.running = true;
    status.backend = "fallback";
    const auto status_bytes = ime::linux::encode_message(status);
    const auto decoded_status = ime::linux::decode_status_response(status_bytes);
    ok = ok && decoded_status.running;
    ok = ok && decoded_status.backend == "fallback";

    ime::linux::ControlRequest stop_request;
    stop_request.operation = "stop";
    const auto stop_bytes = ime::linux::encode_message(stop_request);
    ok = ok && ime::linux::decode_control_request(stop_bytes).operation == "stop";

    ime::linux::PredictResponse response;
    response.candidates.push_back({U'你', U'妳'});
    const auto response_bytes = ime::linux::encode_message(response);
    const auto decoded_response = ime::linux::decode_predict_response(response_bytes);
    ok = ok && decoded_response.candidates.size() == 1;
    ok = ok && decoded_response.candidates.front().size() == 2;
    ok = ok && decoded_response.candidates.front().front() == U'你';

    auto malformed = bytes;
    malformed.push_back(0);
    bool rejected_extra_payload = false;
    try {
        (void)ime::linux::decode_predict_request(malformed);
    } catch (...) {
        rejected_extra_payload = true;
    }
    ok = ok && rejected_extra_payload;

    req.padding[0].chosen_char = U'你';
    const auto chosen_bytes = ime::linux::encode_message(req);
    ok = ok && ime::linux::decode_predict_request(chosen_bytes).padding[0].chosen_char == U'你';

    const std::string invalid_text =
        R"({"context":"","padding":[{"bopomofo":"ㄋㄧˇ","chosen":true,"chosen_char":"你好"}],"type":"predict_request"})";
    const auto invalid_char = frame(invalid_text);
    bool rejected_multi_codepoint = false;
    try {
        (void)ime::linux::decode_predict_request(invalid_char);
    } catch (...) {
        rejected_multi_codepoint = true;
    }
    ok = ok && rejected_multi_codepoint;

    bool rejected_empty_chosen_char = false;
    try {
        (void)ime::linux::decode_predict_request(frame(
            R"({"context":"","padding":[{"bopomofo":"ㄋㄧˇ","chosen":true,"chosen_char":""}],"type":"predict_request"})"));
    } catch (...) {
        rejected_empty_chosen_char = true;
    }
    ok = ok && rejected_empty_chosen_char;

    bool rejected_zero_candidate = false;
    try {
        ime::linux::PredictResponse invalid_response;
        invalid_response.candidates.push_back({0});
        (void)ime::linux::encode_message(invalid_response);
    } catch (...) {
        rejected_zero_candidate = true;
    }
    ok = ok && rejected_zero_candidate;

    bool rejected_decoded_zero_candidate = false;
    try {
        (void)ime::linux::decode_predict_response(
            frame(R"({"candidates":[["\u0000"]],"type":"predict_response"})"));
    } catch (...) {
        rejected_decoded_zero_candidate = true;
    }
    ok = ok && rejected_decoded_zero_candidate;

    bool rejected_wrong_type = false;
    try {
        (void)ime::linux::decode_status_response(bytes);
    } catch (...) {
        rejected_wrong_type = true;
    }
    ok = ok && rejected_wrong_type;

    bool rejected_missing_field = false;
    try {
        (void)ime::linux::decode_predict_request(frame(R"({"context":"","type":"predict_request"})"));
    } catch (...) {
        rejected_missing_field = true;
    }
    ok = ok && rejected_missing_field;

    bool rejected_invalid_scalar = false;
    try {
        (void)ime::linux::char32_to_utf8(0xD800);
    } catch (...) {
        rejected_invalid_scalar = true;
    }
    ok = ok && rejected_invalid_scalar;

    const auto socket_path = std::filesystem::temp_directory_path() / "ime-linux-protocol-test.sock";
    ime::linux::UnixSocketServer server;
    server.bind_listen(socket_path);
    std::thread server_thread([&server]() {
        auto accepted = server.accept_one();
        const auto received = accepted.recv_exact(3);
        accepted.send_all(received);
    });
    auto client = ime::linux::UnixSocketClient{}.connect(socket_path);
    const std::vector<std::uint8_t> payload{1, 2, 3};
    client.send_all(payload);
    ok = ok && client.recv_exact(3) == payload;
    server_thread.join();

    const auto active_socket_path = std::filesystem::temp_directory_path() / "ime-linux-active-test.sock";
    ime::linux::UnixSocketServer active_server;
    active_server.bind_listen(active_socket_path);
    bool rejected_active_socket = false;
    try {
        ime::linux::UnixSocketServer second_server;
        second_server.bind_listen(active_socket_path);
    } catch (...) {
        rejected_active_socket = true;
    }
    ok = ok && rejected_active_socket;

    const auto parentless_socket = std::filesystem::path("ime-linux-parentless-test.sock");
    {
        ime::linux::UnixSocketServer parentless_server;
        parentless_server.bind_listen(parentless_socket);
    }
    ok = ok && !std::filesystem::exists(parentless_socket);

    const auto non_socket_path = std::filesystem::temp_directory_path() / "ime-linux-non-socket-test";
    {
        std::ofstream file(non_socket_path);
        file << "not a socket";
    }
    bool rejected_non_socket = false;
    try {
        ime::linux::UnixSocketServer non_socket_server;
        non_socket_server.bind_listen(non_socket_path);
    } catch (...) {
        rejected_non_socket = true;
    }
    ok = ok && rejected_non_socket;
    ok = ok && std::filesystem::exists(non_socket_path);
    std::filesystem::remove(non_socket_path);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
