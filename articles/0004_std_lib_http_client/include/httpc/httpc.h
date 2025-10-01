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


struct HttpClient {
    HttpProtocolInterface* protocol;
    Error (*connect)(struct HttpClient* self, const char* host, int port);
    Error (*disconnect)(struct HttpClient* self);
    Error (*get)(struct HttpClient* self,
                 HttpRequest* request,
                 HttpResponse* response);
    Error (*post)(struct HttpClient* self,
                  HttpRequest* request,
                  HttpResponse* response);
};

Error http_client_init(
    struct HttpClient* self,
    int transport_type,
    int protocol_type,
    HttpResponseMemoryPolicy policy,
    HttpIoPolicy io_policy
);

Error http_client_init_with_protocol(
    struct HttpClient* self,
    HttpProtocolInterface* protocol
);
void http_client_destroy(struct HttpClient* self);



