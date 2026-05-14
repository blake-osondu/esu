#ifndef ESU_HTTP_H
#define ESU_HTTP_H

#include "esu_value.h"

/* HTTP client: convenience wrappers */
EsuValue *esu_http_get(EsuValue *url);
EsuValue *esu_http_post(EsuValue *url, EsuValue *body);
EsuValue *esu_http_put(EsuValue *url, EsuValue *body);
EsuValue *esu_http_patch(EsuValue *url, EsuValue *body);
EsuValue *esu_http_delete(EsuValue *url);

/* General request: method + URL + options map (body, headers, json) */
EsuValue *esu_http_request(EsuValue *method, EsuValue *url, EsuValue *options);

/* HTTP server */
EsuValue *esu_http_create_server(EsuValue *port);
void esu_http_add_route(EsuValue *server, EsuValue *method,
                        EsuValue *path, EsuValue *handler);
void esu_http_serve_static(EsuValue *server, EsuValue *dir, EsuValue *prefix);
void esu_http_set_before_hook(EsuValue *server, EsuValue *handler);
EsuValue *esu_http_listen(EsuValue *server);

/* Response builders */
EsuValue *esu_http_respond(EsuValue *status, EsuValue *body);
EsuValue *esu_http_redirect(EsuValue *url, EsuValue *permanent);

/* WebSocket server */
void esu_http_add_ws_route(EsuValue *server, EsuValue *path, EsuValue *connect_handler);
void esu_http_add_ws_handler(EsuValue *server, EsuValue *path, EsuValue *message_handler);
void esu_ws_on_message(EsuValue *handler);
void esu_ws_on_close(EsuValue *handler);
void esu_ws_send(EsuValue *connection, EsuValue *message);
void esu_ws_broadcast(EsuValue *message);
void esu_ws_close(EsuValue *connection);

/* WebSocket client (blocking) */
EsuValue *esu_ws_open(EsuValue *url);
void esu_ws_client_send(EsuValue *handle, EsuValue *message);
EsuValue *esu_ws_client_receive(EsuValue *handle);
EsuValue *esu_ws_receive(EsuValue *handle);   /* alias used by __ws_receive */
void esu_ws_client_close(EsuValue *handle);

/* Request accessors */
EsuValue *esu_http_req_path(EsuValue *request);
EsuValue *esu_http_req_method(EsuValue *request);
EsuValue *esu_http_req_body(EsuValue *request);
EsuValue *esu_http_req_header(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_query(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_route_param(EsuValue *request, EsuValue *name);
EsuValue *esu_http_req_cookie(EsuValue *request, EsuValue *name);

#endif
