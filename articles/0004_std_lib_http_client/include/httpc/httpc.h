#pragma once

#include <httpc/transport.h>
#include <httpc/http_protocol.h>


static const struct {
    const int UNIX;
    const int TCP;
} HttpTransportType = {
    .UNIX = 1,
    .TCP = 2,
};

static const struct {
    const int HTTP1;
} HttpProtocolType = {
    .HTTP1 = 1,
};

struct HttpProtocolInterface;
struct HttpClient;

Error http_client_init(struct HttpClient* self, int transport_type, int protocol_type);
Error http_client_init_with_protocol(struct HttpClient* self, struct HttpProtocolInterface* protocol);
void http_client_destroy(struct HttpClient* self);

struct HttpProtocolInterface {
    void* context;
    struct TransportInterface* transport;
    Error (*connect)(void* context, const char* host, int port);
    Error (*disconnect)(void* context);
    Error (*perform_request)(void* context,
                             const HttpRequest* request,
                             HttpResponse* response);
};

struct HttpClient {
    struct HttpProtocolInterface* protocol;
    Error (*get)(struct HttpClient* self,
                 const char* path,
                 HttpResponse* response);
    Error (*post)(struct HttpClient* self,
                  const char* path,
                  const char* body,
                  HttpResponse* response);
};