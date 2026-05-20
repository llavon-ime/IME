#include <windows.h>

#include <asio.hpp>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "engine/llamaEngine.hpp"
#include "pipe/server.hpp"

using namespace imesvc;

static asio::awaitable<bool> read_exact(asio::windows::stream_handle& pipe, void* buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        auto [ec, n] = co_await pipe.async_read_some(
            asio::buffer(static_cast<char*>(buf) + total, size - total), asio::as_tuple(asio::use_awaitable));
        if (ec || n == 0) co_return false;
        total += n;
    }
    co_return true;
}

template <typename T>
static asio::awaitable<bool> read_val(asio::windows::stream_handle& pipe, T& val) {
    co_return co_await read_exact(pipe, &val, sizeof(T));
}

static asio::awaitable<bool> write_exact(asio::windows::stream_handle& pipe, const void* buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        auto [ec, n] = co_await pipe.async_write_some(
            asio::buffer(static_cast<const char*>(buf) + total, size - total), asio::as_tuple(asio::use_awaitable));
        if (ec) co_return false;
        total += n;
    }
    co_return true;
}

template <typename T>
static asio::awaitable<bool> write_val(asio::windows::stream_handle& pipe, const T& val) {
    co_return co_await write_exact(pipe, &val, sizeof(T));
}

static asio::awaitable<std::vector<PaddingEntry>> read_padding(asio::windows::stream_handle& pipe) {
    uint32_t count = 0;
    if (!co_await read_val(pipe, count)) co_return std::vector<PaddingEntry>{};

    std::vector<PaddingEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t type = 0;
        if (!co_await read_val(pipe, type)) co_return std::vector<PaddingEntry>{};

        PaddingEntry e;
        if (type == 0) {
            uint32_t len = 0;
            if (!co_await read_val(pipe, len)) co_return std::vector<PaddingEntry>{};
            e.bpmf.resize(len);
            if (len > 0 && !co_await read_exact(pipe, e.bpmf.data(), len * sizeof(char16_t)))
                co_return std::vector<PaddingEntry>{};
            e.is_chosen = false;
        } else {
            if (!co_await read_val(pipe, e.chosen_char)) co_return std::vector<PaddingEntry>{};
            e.is_chosen = true;
        }
        entries.push_back(std::move(e));
    }
    co_return entries;
}

static asio::awaitable<void> write_response(asio::windows::stream_handle& pipe,
                                            const std::vector<PredictResult>& results) {
    uint32_t count = static_cast<uint32_t>(results.size());
    if (!co_await write_val(pipe, count)) co_return;

    for (auto& r : results) {
        uint32_t nc = static_cast<uint32_t>(r.candidates.size());
        if (!co_await write_val(pipe, nc)) co_return;
        for (auto& [c, _] : r.candidates) {
            if (!co_await write_val(pipe, c)) co_return;
        }
    }
}

static asio::awaitable<void> handle_client(asio::windows::stream_handle pipe) {
    std::unique_ptr<LlamaEngine> engine;
    try {
        engine = std::make_unique<LlamaEngine>();
    } catch (const std::exception& e) {
        std::cerr << "[ERR] engine init: " << e.what() << std::endl;
        co_return;
    }

    while (true) {
        uint32_t ctx_len = 0;
        if (!co_await read_val(pipe, ctx_len)) break;

        std::u16string context;
        if (ctx_len > 0) {
            context.resize(ctx_len);
            if (!co_await read_exact(pipe, context.data(), ctx_len * sizeof(char16_t))) break;
        }

        auto padding = co_await read_padding(pipe);
        if (padding.empty() && ctx_len == 0) break;

        {
            std::vector<PredictResult> results;
            bool ok = false;
            try {
                results = engine->predict(context, padding);
                ok = true;
            } catch (const std::exception& e) {
                std::cerr << "[ERR] predict: " << e.what() << std::endl;
            }
            if (ok) {
                co_await write_response(pipe, results);
            } else {
                uint32_t zero = 0;
                co_await write_val(pipe, zero);
            }
        }
    }
}

static asio::awaitable<void> listener(asio::io_context& io_ctx) {
    auto executor = co_await asio::this_coro::executor;

    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "[ERR] CreateNamedPipe failed: " << GetLastError() << std::endl;
            co_return;
        }

        OVERLAPPED ol{};
        ol.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL connected = ConnectNamedPipe(hPipe, &ol);
        DWORD err = GetLastError();

        if (!connected && err == ERROR_PIPE_CONNECTED) {
            // 用戶端在 ConnectNamedPipe 之前就連上了
            CloseHandle(ol.hEvent);
        } else if (!connected && err == ERROR_IO_PENDING) {
            asio::windows::object_handle ev(executor);
            ev.assign(ol.hEvent);
            co_await ev.async_wait(asio::use_awaitable);
        } else {
            std::cerr << "[ERR] ConnectNamedPipe failed: " << err << std::endl;
            CloseHandle(ol.hEvent);
            CloseHandle(hPipe);
            continue;
        }

        std::cerr << "[SRV] client connected" << std::endl;

        asio::windows::stream_handle stream(executor, hPipe);
        co_spawn(executor, handle_client(std::move(stream)), asio::detached);
    }
}

int main() {
    SetConsoleOutputCP(65001);
    std::cerr << "[SRV] IME Service starting" << std::endl;

    try {
        ModelManager::initialize();
        std::cerr << "[SRV] model loaded" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ERR] model load failed: " << e.what() << std::endl;
        return 1;
    }

    asio::io_context io_ctx;
    co_spawn(io_ctx, listener(io_ctx), asio::detached);
    io_ctx.run();

    return 0;
}
