#include "http_server.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Socket handle aliases: Winsock uses SOCKET, POSIX uses int.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t invalid_socket = -1;
#endif

namespace opendoc::serve {
    namespace {
        // ASCII-lowercases a string; the cast keeps bytes >= 0x80 out of ctype's UB range.
        std::string lower(std::string s) {
            for (auto &c: s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }

            return s;
        }

        // Maps a file extension to its Content-Type; unknown types fall back to octet-stream.
        const char *mime_for_path(const std::filesystem::path &p) {
            const auto e = lower(p.extension().string());
            if (e == ".html" || e == ".htm") {
                return "text/html; charset=utf-8";
            }
            if (e == ".css") {
                return "text/css; charset=utf-8";
            }
            if (e == ".js" || e == ".mjs") {
                return "application/javascript; charset=utf-8";
            }
            if (e == ".json") {
                return "application/json; charset=utf-8";
            }
            if (e == ".xml") {
                return "application/xml; charset=utf-8";
            }
            if (e == ".svg") {
                return "image/svg+xml";
            }
            if (e == ".png") {
                return "image/png";
            }
            if (e == ".jpg" || e == ".jpeg") {
                return "image/jpeg";
            }
            if (e == ".gif") {
                return "image/gif";
            }
            if (e == ".webp") {
                return "image/webp";
            }
            if (e == ".ico") {
                return "image/x-icon";
            }
            if (e == ".txt" || e == ".md") {
                return "text/plain; charset=utf-8";
            }
            if (e == ".woff") {
                return "font/woff";
            }
            if (e == ".woff2") {
                return "font/woff2";
            }
            if (e == ".map") {
                return "application/json";
            }
            if (e == ".pdf") {
                return "application/pdf";
            }
            return "application/octet-stream";
        }

        // Decodes %XX escapes and '+' as space; malformed escapes are copied through verbatim.
        std::string percent_decode(const std::string_view in) {
            std::string out;
            out.reserve(in.size());
            for (std::size_t i = 0; i < in.size(); ++i) {
                if (in[i] == '%' && i + 2 < in.size() &&
                    std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                    const auto hex = std::string(in.substr(i + 1, 2));
                    out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
                    i += 2;
                } else if (in[i] == '+') {
                    out.push_back(' ');
                } else {
                    out.push_back(in[i]);
                }
            }

            return out;
        }

        // Closes a descriptor, honoring the Winsock/POSIX difference; ignores invalid handles.
        void close_socket(const socket_t s) {
            if (s == invalid_socket) {
                return;
            }

#ifdef _WIN32
            closesocket(s);
#else
            ::close(s);
#endif
        }

        // Loops over partial writes; send() may transmit fewer bytes than requested.
        bool send_all(const socket_t s, const char *data, const std::size_t len) {
            std::size_t sent = 0;
            while (sent < len) {
#ifdef _WIN32
                const int n = send(s, data + sent, static_cast<int>(len - sent), 0);
#else
                const ssize_t n = send(s, data + sent, len - sent, 0);
#endif
                if (n <= 0) {
                    return false;
                }

                sent += static_cast<std::size_t>(n);
            }

            return true;
        }

        // Convenience wrapper that writes a whole std::string.
        bool send_str(const socket_t s, const std::string &str) {
            return send_all(s, str.data(), str.size());
        }

        // Reason phrase for the status codes this server can emit.
        std::string status_text(const int code) {
            switch (code) {
                case 200:
                    return "OK";
                case 301:
                    return "Moved Permanently";
                case 304:
                    return "Not Modified";
                case 400:
                    return "Bad Request";
                case 403:
                    return "Forbidden";
                case 404:
                    return "Not Found";
                case 405:
                    return "Method Not Allowed";
                case 500:
                    return "Internal Server Error";
                default:
                    return "Unknown";
            }
        }

        // Writes a full HTTP/1.1 response with headers, then the body, then closes later.
        void send_response(const socket_t client, const int status, const std::string &content_type,
                           const std::string &body, const std::vector<std::string> &extra = {}) {
            std::ostringstream oss;
            oss << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\n"
                    << "Content-Type: " << content_type << "\r\n"
                    << "Content-Length: " << body.size() << "\r\n"
                    << "Connection: close\r\n"
                    << "Cache-Control: no-cache\r\n";
            for (const auto &h: extra) {
                oss << h << "\r\n";
            }
            oss << "\r\n";

            const auto head = oss.str();
            send_str(client, head);

            if (!body.empty()) {
                send_all(client, body.data(), body.size());
            }
        }

        // Self-contained 404 page; all styling is inline so it renders with no site assets.
        const std::string not_found_page =
                "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                "<title>404 · Not Found</title>"
                "<style>"
                ":root{color-scheme:light dark}"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Inter,"
                "Helvetica,Arial,sans-serif;display:grid;place-items:center;min-height:100vh;"
                "margin:0;background:#fff;color:#0a0a0a;"
                "-webkit-font-smoothing:antialiased}"
                ".box{text-align:center;padding:2rem}"
                "h1{font-size:3rem;font-weight:600;letter-spacing:-.03em;margin:0 0 .5rem}"
                "p{margin:0 0 1.5rem;color:#666;font-size:.9375rem;line-height:1.6}"
                "a{display:inline-block;padding:.5rem 1rem;border:1px solid rgba(0,0,0,.14);"
                "border-radius:6px;color:#0a0a0a;text-decoration:none;font-size:.875rem;"
                "font-weight:500;transition:background .12s}"
                "a:hover{background:#f2f2f2}"
                "@media(prefers-color-scheme:dark){"
                "body{background:#0a0a0a;color:#ededed}"
                "p{color:#a1a1a1}"
                "a{border-color:rgba(255,255,255,.16);color:#ededed}"
                "a:hover{background:#1a1a1a}}"
                "</style></head>"
                "<body><div class=\"box\"><h1>404</h1>"
                "<p>This page could not be found.</p>"
                "<a href=\"/\">Back to home</a></div></body></html>";
    }

    // Private state behind HttpServer's PIMPL; the accept thread lives entirely here.
    struct HttpServer::Impl {
        ServerOptions opts;
        socket_t listen_sock = invalid_socket;
        std::atomic<bool> running{false};
        std::atomic<bool> stop_requested{false};
        std::thread accept_thread;
        std::uint16_t bound_port = 0;
        std::filesystem::path root_canon;

        void handle_client(socket_t client) const;

        void accept_loop();
    };

    // Reads one request, resolves it under the site root, and replies; always closes client.
    void HttpServer::Impl::handle_client(socket_t client) const {
        // Read until the header terminator or the 64 KiB cap, whichever comes first.
        std::string req;
        char buf[4096];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
#ifdef _WIN32
            const int n = recv(client, buf, sizeof(buf), 0);
#else
            const ssize_t n = recv(client, buf, sizeof(buf), 0);
#endif
            if (n <= 0) {
                break;
            }

            req.append(buf, static_cast<std::size_t>(n));
        }

        if (req.empty()) {
            close_socket(client);
            return;
        }

        const auto line_end = req.find("\r\n");
        const std::string request_line = req.substr(0, line_end);
        std::istringstream iss(request_line);
        std::string method, target, version;
        iss >> method >> target >> version;

        if (method.empty() || target.empty()) {
            send_response(client, 400, "text/plain; charset=utf-8", "Bad Request\n");
            close_socket(client);
            return;
        }

        if (method != "GET" && method != "HEAD") {
            send_response(client, 405, "text/plain; charset=utf-8", "Method Not Allowed\n",
                          {"Allow: GET, HEAD"});
            close_socket(client);
            return;
        }

        std::string path = target;
        if (auto q = path.find_first_of("?#"); q != std::string::npos) {
            path = path.substr(0, q);
        }
        path = percent_decode(path);

        if (path.empty() || path[0] != '/') {
            send_response(client, 400, "text/plain; charset=utf-8", "Bad Request\n");
            close_socket(client);
            return;
        }

        std::vector<std::string> parts;
        std::string seg;
        for (const char c: path) {
            if (c == '/') {
                if (!seg.empty()) {
                    parts.push_back(seg);
                }
                seg.clear();
            } else {
                seg.push_back(c);
            }
        }
        if (!seg.empty()) {
            parts.push_back(seg);
        }

        // Resolve "." and ".." lexically; a ".." that climbs past the root is rejected
        // outright, since it can only be an attempt to escape the document directory.
        std::vector<std::string> norm;
        for (const auto &p: parts) {
            if (p == ".") {
                continue;
            }

            if (p == "..") {
                if (norm.empty()) {
                    send_response(client, 403, "text/plain; charset=utf-8",
                                  "Forbidden\n");
                    close_socket(client);
                    return;
                }

                norm.pop_back();
                continue;
            }

            norm.push_back(p);
        }

        std::filesystem::path rel;
        for (const auto &p: norm) {
            rel /= p;
        }

        // Path-traversal guard: even after normalizing, the target must stay inside
        // root_canon (string prefix match must also land on a separator boundary).
        auto file = (root_canon / rel).lexically_normal();

        {
            const auto root_s = root_canon.generic_string();

            if (const auto file_s = file.generic_string();
                file_s != root_s &&
                !(file_s.size() > root_s.size() &&
                  file_s.compare(0, root_s.size(), root_s) == 0 &&
                  (file_s[root_s.size()] == '/' || file_s[root_s.size()] == '\\'))) {
                send_response(client, 403, "text/plain; charset=utf-8", "Forbidden\n");
                close_socket(client);
                return;
            }
        }

        // Directories serve their index.html; anything else 404s with the styled page.
        std::error_code ec;
        if (std::filesystem::is_directory(file, ec)) {
            if (const auto idx = file / "index.html"; std::filesystem::exists(idx, ec)) {
                file = idx;
            } else {
                send_response(client, 404, "text/html; charset=utf-8", not_found_page);
                close_socket(client);
                return;
            }
        }

        if (!std::filesystem::exists(file, ec)) {
            if (!file.has_extension()) {
                if (const auto as_html = std::filesystem::path(file.string() + ".html");
                    std::filesystem::exists(as_html, ec)) {
                    file = as_html;
                } else if (const auto as_index = file / "index.html";
                    std::filesystem::exists(as_index, ec)) {
                    file = as_index;
                }
            }

            // Extensionless URLs fall back to pretty routes: page.html, then page/index.html.
            if (!std::filesystem::exists(file, ec)) {
                send_response(client, 404, "text/html; charset=utf-8", not_found_page);
                close_socket(client);
                return;
            }
        }

        if (!std::filesystem::is_regular_file(file, ec)) {
            send_response(client, 404, "text/html; charset=utf-8", not_found_page);
            close_socket(client);
            return;
        }

        std::ifstream ifs(file, std::ios::binary);
        if (!ifs) {
            send_response(client, 404, "text/html; charset=utf-8", not_found_page);
            close_socket(client);
            return;
        }

        std::ostringstream body;
        body << ifs.rdbuf();
        const std::string data = body.str();

        // HEAD gets the same headers as GET but must not send a body.
        if (method == "HEAD") {
            std::ostringstream oss;
            oss << "HTTP/1.1 200 OK\r\n"
                    << "Content-Type: " << mime_for_path(file) << "\r\n"
                    << "Content-Length: " << data.size() << "\r\n"
                    << "Connection: close\r\n\r\n";
            send_str(client, oss.str());
        } else {
            send_response(client, 200, mime_for_path(file), data);
        }
        close_socket(client);
    }

    // Accepts connections until stop is requested; the select() timeout keeps it from
    // blocking forever so shutdown does not depend on a wakeup connection.
    void HttpServer::Impl::accept_loop() {
        while (!stop_requested.load()) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(listen_sock, &fds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;

            const int rv = select(static_cast<int>(listen_sock) + 1, &fds, nullptr, nullptr,
                                  &tv);

            if (rv <= 0) {
                continue;
            }

            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            const socket_t client = accept(listen_sock, reinterpret_cast<sockaddr *>(&client_addr),
                                           &len);

            if (client == invalid_socket) {
                if (stop_requested.load()) {
                    break;
                }
                continue;
            }

            handle_client(client);
        }

        running.store(false);
    }

    HttpServer::HttpServer(ServerOptions opts) : impl_(std::make_unique<Impl>()) {
        impl_->opts = std::move(opts);
    }

    // Destroys the server, which stops it first so no thread outlives this object.
    HttpServer::~HttpServer() {
        stop();
    }

    // Binds the socket and starts serving on a background thread; safe to call twice.
    void HttpServer::start() const {
        if (impl_->running.load()) {
            return;
        }

        // Winsock must be initialized once per process before any socket call.
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            throw ServerError("WSAStartup failed");
        }
#endif

        std::error_code ec;
        if (!std::filesystem::exists(impl_->opts.root, ec)) {
            throw ServerError("site directory does not exist: " +
                              impl_->opts.root.string() +
                              " (run `opendoc build` first)");
        }

        impl_->root_canon = std::filesystem::weakly_canonical(impl_->opts.root, ec);

        impl_->listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);

        if (impl_->listen_sock == invalid_socket) {
            throw ServerError("socket() failed");
        }

        constexpr int yes = 1;
        setsockopt(impl_->listen_sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(impl_->opts.port);

        // Default and "localhost" bind to loopback only, keeping previews off the LAN.
        if (impl_->opts.host.empty() || impl_->opts.host == "localhost") {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            if (inet_pton(AF_INET, impl_->opts.host.c_str(), &addr.sin_addr) != 1) {
                close_socket(impl_->listen_sock);
                impl_->listen_sock = invalid_socket;
                throw ServerError("invalid host address: " + impl_->opts.host);
            }
        }

        if (bind(impl_->listen_sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            close_socket(impl_->listen_sock);
            impl_->listen_sock = invalid_socket;
#ifdef _WIN32
            const int err = WSAGetLastError();
            throw ServerError("bind failed on " + impl_->opts.host + ":" +
                              std::to_string(impl_->opts.port) + " (error " +
                              std::to_string(err) + ")");
#else
            throw ServerError("bind failed on " + impl_->opts.host + ":" +
                              std::to_string(impl_->opts.port));
#endif
        }

        // The OS may assign an ephemeral port, so report what was actually bound.
        sockaddr_in bound{};
        socklen_t blen = sizeof(bound);
        if (getsockname(impl_->listen_sock, reinterpret_cast<sockaddr *>(&bound), &blen) == 0) {
            impl_->bound_port = ntohs(bound.sin_port);
        } else {
            impl_->bound_port = impl_->opts.port;
        }

        if (listen(impl_->listen_sock, 16) != 0) {
            close_socket(impl_->listen_sock);
            impl_->listen_sock = invalid_socket;
            throw ServerError("listen failed");
        }

        impl_->stop_requested.store(false);
        impl_->running.store(true);
        impl_->accept_thread = std::thread([this] {
            impl_->accept_loop();
        });
    }

    // Requests shutdown, closes the listening socket to unblock accept, and joins.
    // Idempotent and noexcept; calling it before start() is harmless.
    void HttpServer::stop() const noexcept {
        if (!impl_) {
            return;
        }

        impl_->stop_requested.store(true);

        if (impl_->listen_sock != invalid_socket) {
            close_socket(impl_->listen_sock);
            impl_->listen_sock = invalid_socket;
        }

        if (impl_->accept_thread.joinable()) {
            impl_->accept_thread.join();
        }

        // WSACleanup only after the thread is done, and at most once per process.
#ifdef _WIN32
        if (impl_->running.load() == false) {
            static bool cleaned = false;
            if (!cleaned) {
                WSACleanup();
                cleaned = true;
            }
        }
#endif

        impl_->running.store(false);
    }

    // Joins the accept thread, i.e. blocks until the server has fully stopped.
    void HttpServer::wait_until_stopped() const {
        if (impl_->accept_thread.joinable()) {
            impl_->accept_thread.join();
        }
    }

    // Port reported by getsockname() after start(), else the requested one.
    std::uint16_t HttpServer::bound_port() const noexcept {
        return impl_->bound_port;
    }

    // True only while the accept loop is actually running.
    bool HttpServer::is_running() const noexcept {
        return impl_->running.load();
    }

    namespace {
        // Single server instance the signal handlers reach; set by install_stop_signal().
        HttpServer *g_server_for_signal = nullptr;

#ifdef _WIN32

        // Console handler for Ctrl+C/Ctrl+Break; stops the server and swallows the event.
        BOOL WINAPI ctrl_handler(const DWORD type) {
            if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
                if (g_server_for_signal) {
                    g_server_for_signal->stop();
                }
                return TRUE;
            }
            return FALSE;
        }
#else

        // POSIX signal handler; only calls stop(), which is async-signal safe enough here.
        void posix_handler(int) {
            if (g_server_for_signal) {
                g_server_for_signal->stop();
            }
        }
#endif
    }

    // Hooks OS stop signals so Ctrl+C shuts the server down cleanly instead of killing it.
    void install_stop_signal(HttpServer &server) {
        g_server_for_signal = &server;
#ifdef _WIN32
        SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
        std::signal(SIGINT, posix_handler);
        std::signal(SIGTERM, posix_handler);
#endif
    }
}
