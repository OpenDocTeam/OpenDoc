#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace opendoc::serve {
    // Startup or runtime failure of the dev server, e.g. bind() refused the port.
    class ServerError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Listen address, port, and the directory of built files to serve.
    struct ServerOptions {
        std::string host = "127.0.0.1";
        std::uint16_t port = 8000;
        std::filesystem::path root;
    };

    // Blocking TCP file server for locally previewing a built site.
    // Cheap to move around via PIMPL; safe to destroy, as the destructor stops it.
    class HttpServer {
    public:
        // Stores the options; no sockets are created until start().
        explicit HttpServer(ServerOptions opts);

        // Stops the server if it is still running.
        ~HttpServer();

        HttpServer(const HttpServer &) = delete;

        HttpServer &operator=(const HttpServer &) = delete;

        // Binds, listens, and spawns the accept thread; no-op if already running.
        void start() const;

        // Asks the accept loop to exit without blocking; never joins, so it is
        // safe to call from a signal handler (and from any thread).
        void request_stop() const noexcept;

        // Signals the accept loop to exit, joins it, and releases the socket;
        // safe to call repeatedly, including before start().
        void stop() const noexcept;

        // Blocks until the accept thread has finished.
        void wait_until_stopped() const;

        // Actual port after start(), which is what port 0 resolves to.
        [[nodiscard]] std::uint16_t bound_port() const noexcept;

        [[nodiscard]] bool is_running() const noexcept;

    private:
        // Opaque implementation, defined in http_server.cpp.
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // Routes Ctrl+C/SIGINT/SIGTERM to server.request_stop().
    void install_stop_signal(HttpServer &server);
}
