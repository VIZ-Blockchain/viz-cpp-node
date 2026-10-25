// Offline regression: real websocketpp parser/handshake/state machine, no sockets.
#include <websocketpp/config/core.hpp>
#include <websocketpp/server.hpp>
#include <graphene/plugins/node_admin/http_only.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using server_type = websocketpp::server<websocketpp::config::core>;

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct result {
    std::string response;
    int http_calls = 0;
    int opens = 0;
    int failures = 0;
    websocketpp::session::state::value state;
};

result request(const std::string &input, bool http_only) {
    server_type server;
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    result r;
    std::ostringstream output;
    server.register_ostream(&output);
    auto http = [&](websocketpp::connection_hdl hdl) {
        ++r.http_calls;
        auto con = server.get_con_from_hdl(hdl);
        // Model the existing routing callback: ordinary responses remain its job.
        const bool authorized = con->get_request().get_header("Authorization") == "Bearer test";
        con->set_status(authorized ? websocketpp::http::status_code::ok
                                   : websocketpp::http::status_code::unauthorized);
        con->set_body(authorized ? "normal HTTP" : "unauthorized");
        con->replace_header("Connection", "close");
    };
    if (http_only) graphene::plugins::node_admin::util::configure_http_only(server, http);
    else server.set_http_handler(http); // Negative control: baseline accepts upgrades.
    server.set_open_handler([&](websocketpp::connection_hdl) { ++r.opens; });
    server.set_fail_handler([&](websocketpp::connection_hdl) { ++r.failures; });
    auto con = server.get_connection();
    require(bool(con), "get_connection failed");
    con->start();
    require(con->read_all(input.data(), input.size()) == input.size(), "request not consumed");
    r.response = output.str();
    r.state = con->get_state();
    return r;
}

const std::string upgrade =
    "GET /admin HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
    "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
}

int main() {
    try {
        const auto baseline = request(upgrade + "\r\n", false);
        require(baseline.response.find("HTTP/1.1 101 ") == 0, "negative control did not upgrade");
        require(baseline.http_calls == 0 && baseline.opens == 1, "baseline route not confirmed");
        require(baseline.state == websocketpp::session::state::open, "baseline did not remain open");
        std::cout << "Baseline: unauthenticated upgrade opens WebSocket, skips HTTP callback\n";
        for (const std::string &auth : {std::string(), std::string("Authorization: Bearer test\r\n")}) {
            const auto rejected = request(upgrade + auth + "\r\n", true);
            require(rejected.response.find("HTTP/1.1 403 ") == 0, "HTTP-only listener must reject upgrades with 403");
            require(rejected.http_calls == 0 && rejected.opens == 0 && rejected.failures == 1,
                    "upgrade rejection must bypass HTTP callback without opening WebSocket");
            require(rejected.state == websocketpp::session::state::closed, "rejected upgrade did not close");
        }
        std::cout << "HTTP-only: upgrades rejected and closed, with and without Authorization\n";
        for (const std::string &auth : {std::string(), std::string("Authorization: Bearer test\r\n")}) {
            const std::string http = "POST /admin HTTP/1.1\r\nHost: localhost\r\nContent-Length: 2\r\n" + auth + "\r\n{}";
            const auto before = request(http, false);
            const auto after = request(http, true);
            require(after.response == before.response, "ordinary HTTP response changed");
            require(after.http_calls == 1 && after.opens == 0, "ordinary HTTP callback not called");
            require(after.state == websocketpp::session::state::closed, "ordinary HTTP did not close");
            require(after.response.find(auth.empty() ? "HTTP/1.1 401 " : "HTTP/1.1 200 ") == 0,
                    "ordinary HTTP status changed");
        }
        std::cout << "Ordinary HTTP: callback, 200/401 responses and closure preserved\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
