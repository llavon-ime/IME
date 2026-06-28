#include "config/config.hpp"
#include "ipc/unix_socket.hpp"
#include "protocol/json_codec.hpp"
#include "service/service_app.hpp"
#include "text/utf.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>
#include <poll.h>
#include <unistd.h>

namespace {

constexpr std::uint32_t kMaxFramePayloadBytes = 1024 * 1024;

nlohmann::json status_json(const ime::linux::StatusResponse& status) {
    return nlohmann::json{{"running", status.running},
                          {"model_loaded", status.model_loaded},
                          {"backend", status.backend},
                          {"model_path", status.model_path},
                          {"error", status.error}};
}

ime::linux::ByteVector recv_message(const ime::linux::UnixSocketConnection& connection) {
    auto header = connection.recv_exact(4);
    const auto length = static_cast<std::uint32_t>(header[0]) |
                        (static_cast<std::uint32_t>(header[1]) << 8U) |
                        (static_cast<std::uint32_t>(header[2]) << 16U) |
                        (static_cast<std::uint32_t>(header[3]) << 24U);
    if (length > kMaxFramePayloadBytes) throw std::runtime_error("protocol frame is too large");
    auto payload = connection.recv_exact(length);
    header.insert(header.end(), payload.begin(), payload.end());
    return header;
}

class PidFile {
public:
    explicit PidFile(std::filesystem::path path) : path_(std::move(path)) {
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream pid(path_);
        if (!pid) {
            std::filesystem::remove(path_);
            throw std::runtime_error("failed to open PID file: " + path_.string());
        }
        pid << getpid() << '\n';
        if (!pid) {
            std::filesystem::remove(path_);
            throw std::runtime_error("failed to write PID file: " + path_.string());
        }
    }

    ~PidFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

private:
    std::filesystem::path path_;
};

void send_status(const ime::linux::UnixSocketConnection& connection, ime::linux::ServiceApp& app) {
    connection.send_all(ime::linux::encode_message(app.handle_status()));
}

void handle_connection(const ime::linux::UnixSocketConnection& connection, ime::linux::ServiceApp& app) {
    const auto message = recv_message(connection);
    const auto type = ime::linux::decode_message_type(message);

    if (type == "control_request") {
        const auto request = ime::linux::decode_control_request(message);
        if (request.operation == "stop") app.handle_stop();
        send_status(connection, app);
        return;
    }

    if (type == "predict_request") {
        const auto request = ime::linux::decode_predict_request(message);
        connection.send_all(ime::linux::encode_message(app.handle_predict(request)));
        return;
    }

    ime::linux::StatusResponse status = app.handle_status();
    status.error = "unknown message type: " + type;
    connection.send_all(ime::linux::encode_message(status));
}

int print_status(ime::linux::ServiceApp& app) {
    std::cout << status_json(app.handle_status()).dump() << '\n';
    return EXIT_SUCCESS;
}

int smoke_predict(ime::linux::ServiceApp& app, const std::string& bopomofo) {
    ime::linux::PredictRequest request;
    request.padding.push_back({false, ime::linux::utf8_to_u16(bopomofo), 0});

    const auto response = app.handle_predict(request);
    if (response.candidates.empty() || response.candidates.front().empty()) return EXIT_FAILURE;

    for (size_t i = 0; i < response.candidates.front().size(); ++i) {
        if (i != 0) std::cout << ' ';
        std::cout << ime::linux::char32_to_utf8(response.candidates.front()[i]);
    }
    std::cout << '\n';
    return EXIT_SUCCESS;
}

int run(int argc, char** argv) {
    auto config = ime::linux::load_config();
    if (const char* model_path = std::getenv("IME_LINUX_MODEL_PATH")) config.model_path = model_path;
    ime::linux::ServiceApp app(config);

    if (argc >= 2 && std::string(argv[1]) == "--status") return print_status(app);
    if (argc >= 2 && std::string(argv[1]) == "--smoke-predict") {
        if (argc < 3) throw std::runtime_error("--smoke-predict requires a bopomofo syllable");
        return smoke_predict(app, argv[2]);
    }

    ime::linux::UnixSocketServer server;
    server.bind_listen(ime::linux::socket_path());
    PidFile pid_file(ime::linux::pid_path());

    while (!app.stop_requested()) {
        pollfd fd{server.native_handle(), POLLIN, 0};
        const int rc = ::poll(&fd, 1, 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "poll failed");
        }

        if (rc == 0) {
            if (app.expired_idle_timeout()) break;
            continue;
        }

        auto connection = server.accept_one();
        try {
            handle_connection(connection, app);
        } catch (const std::exception& error) {
            ime::linux::StatusResponse status = app.handle_status();
            status.error = error.what();
            try {
                connection.send_all(ime::linux::encode_message(status));
            } catch (...) {
                // The client may have disconnected before sending a complete frame.
            }
        }

        if (app.expired_idle_timeout()) break;
    }

    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
