#pragma once

#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/http/constants.hpp>

namespace graphene { namespace plugins { namespace node_admin { namespace util {

// Shared by the Asio listener and the offline websocketpp transport regression.
template <typename Server, typename HttpHandler>
void configure_http_only(Server &server, HttpHandler handler) {
    server.set_http_handler(handler);
    // websocketpp accepts valid WebSocket handshakes by default, without calling
    // the HTTP handler. This listener has no WebSocket API: reject even when an
    // Authorization header is present, then let websocketpp close the transport.
    server.set_validate_handler([&server](websocketpp::connection_hdl hdl) {
        auto con = server.get_con_from_hdl(hdl);
        con->set_status(websocketpp::http::status_code::forbidden);
        con->set_body("WebSocket upgrades are not supported on the admin HTTP listener");
        con->replace_header("Connection", "close");
        return false;
    });
}

} } } } // graphene::plugins::node_admin::util
