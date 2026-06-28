#include "engine/service_client.hpp"
#include "ipc/unix_socket.hpp"
#include "protocol/json_codec.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

class ScopedEnv {
public:
    ScopedEnv(std::string name, std::string value) : name_(std::move(name)) {
        if (const char* previous = std::getenv(name_.c_str())) {
            had_previous_ = true;
            previous_ = previous;
        }
        setenv(name_.c_str(), value.c_str(), 1);
    }

    ~ScopedEnv() {
        if (had_previous_) {
            setenv(name_.c_str(), previous_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

std::vector<std::uint8_t> recv_message(const ime::linux::UnixSocketConnection& connection) {
    auto header = connection.recv_exact(4);
    const auto length = static_cast<std::uint32_t>(header[0]) |
                        (static_cast<std::uint32_t>(header[1]) << 8U) |
                        (static_cast<std::uint32_t>(header[2]) << 16U) |
                        (static_cast<std::uint32_t>(header[3]) << 24U);
    auto payload = connection.recv_exact(length);
    header.insert(header.end(), payload.begin(), payload.end());
    return header;
}

}  // namespace

int run_service_client_tests() {
    bool ok = true;

    const auto unique_suffix = std::to_string(getpid()) + "-" +
                               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto missing_service_path = std::filesystem::temp_directory_path() / ("ime-linux-test-missing-service-" + unique_suffix);
    std::filesystem::remove(missing_service_path);
    ScopedEnv service_path_override("IME_LINUX_SERVICE_PATH", missing_service_path.string());

    const auto unavailable_socket = std::filesystem::temp_directory_path() / ("ime-linux-client-unavailable-" + unique_suffix + ".sock");
    std::filesystem::remove(unavailable_socket);
    ime::linux::ServiceClient client(unavailable_socket);
    ime::linux::PredictRequest req;
    req.padding.push_back({false, u"ㄋㄧˇ", 0});
    const auto state = client.request_predict_async(req);
    ok = ok && (state == ime::linux::PredictState::Pending || state == ime::linux::PredictState::Unavailable);

    ime::linux::ServiceClient callback_client(unavailable_socket);
    std::mutex callback_mutex;
    bool callback_called = false;
    auto callback_state = ime::linux::PredictState::Pending;
    (void)callback_client.request_predict_async(req, [&](ime::linux::PredictState completed_state) {
        std::lock_guard lock(callback_mutex);
        callback_called = true;
        callback_state = completed_state;
    });
    for (int i = 0; i < 20; ++i) {
        {
            std::lock_guard lock(callback_mutex);
            if (callback_called) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    {
        std::lock_guard lock(callback_mutex);
        ok = ok && callback_called;
        ok = ok && callback_state == ime::linux::PredictState::Unavailable;
    }

    const auto slow_socket = std::filesystem::temp_directory_path() / ("ime-linux-client-slow-" + unique_suffix + ".sock");
    ime::linux::UnixSocketServer server;
    server.bind_listen(slow_socket);
    std::thread server_thread([&server]() {
        auto connection = server.accept_one();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    });

    ime::linux::ServiceClient slow_client(slow_socket);
    (void)slow_client.request_predict_async(req);
    const auto start = std::chrono::steady_clock::now();
    ok = ok && !slow_client.latest_response().has_value();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    ok = ok && elapsed < std::chrono::milliseconds(100);
    server_thread.join();

    const auto stop_socket = std::filesystem::temp_directory_path() / ("ime-linux-client-stop-" + unique_suffix + ".sock");
    ime::linux::UnixSocketServer stop_server;
    stop_server.bind_listen(stop_socket);
    bool stop_seen = false;
    std::thread stop_thread([&]() {
        auto connection = stop_server.accept_one();
        const auto request = ime::linux::decode_control_request(recv_message(connection));
        stop_seen = request.operation == "stop";
        ime::linux::StatusResponse response;
        response.running = false;
        response.backend = "fallback";
        connection.send_all(ime::linux::encode_message(response));
    });

    ime::linux::ServiceClient stop_client(stop_socket);
    const auto stop_status = stop_client.stop();
    stop_thread.join();
    ok = ok && stop_seen;
    ok = ok && !stop_status.running;

    const auto stopped_socket = std::filesystem::temp_directory_path() / ("ime-linux-client-stopped-" + unique_suffix + ".sock");
    ime::linux::UnixSocketServer stopped_server;
    stopped_server.bind_listen(stopped_socket);
    std::thread stopped_thread([&]() {
        auto connection = stopped_server.accept_one();
        try {
            const auto request = ime::linux::decode_control_request(recv_message(connection));
            if (request.operation == "status") {
                ime::linux::StatusResponse response;
                response.running = false;
                response.backend = "fallback";
                connection.send_all(ime::linux::encode_message(response));
            }
        } catch (...) {
        }
    });

    ime::linux::ServiceClient stopped_client(stopped_socket);
    ok = ok && !stopped_client.start_service_if_needed();
    stopped_thread.join();

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
