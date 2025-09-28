#include <string.h>
#include <stdio.h>

#include <httpc/httpc.h>
#include <httpc/tcp_transport.h>
#include <httpc/unix_transport.h>
#include <httpc/http1_protocol.h>

static Error http_client_connect(struct HttpClient* self, const char* host, int port) {
    return self->protocol->connect(self->protocol->context, host, port);
}

static Error http_client_disconnect(struct HttpClient* self) {
    return self->protocol->disconnect(self->protocol->context);
}

static Error http_client_get(struct HttpClient* self,
                             HttpRequest* request,
                             HttpResponse* response) {
    if (self == nullptr || request == nullptr || response == nullptr || request->path == nullptr) {
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INVALID_REQUEST_SYNTAX};
    }

    if (request->body != nullptr) {
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INVALID_REQUEST_SYNTAX};
    }

    request->method = HTTP_GET;

    return self->protocol->perform_request(self->protocol->context, request, response);
}

static Error http_client_post(struct HttpClient* self,
                              HttpRequest* request,
                              HttpResponse* response) {
    if (self == nullptr || request == nullptr || response == nullptr || request->path == nullptr || request->body == nullptr) {
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INVALID_REQUEST_SYNTAX};
    }

    int content_length_found = 0;
    for (size_t i = 0; i < request->num_headers; ++i) {
        // TODO: dependency injection for strcasecmp
        if (request->headers[i].key != nullptr && strcasecmp(request->headers[i].key, "Content-Length") == 0) {
            content_length_found = 1;
            break;
        }
    }

    if (!content_length_found) {
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INVALID_REQUEST_SYNTAX};
    }

    request->method = HTTP_POST;

    return self->protocol->perform_request(self->protocol->context, request, response);
}

Error http_client_init_with_protocol(struct HttpClient* self, HttpProtocolInterface* protocol, HttpResponseMemoryPolicy policy) {
    self->protocol = protocol;
    self->connect = http_client_connect;
    self->disconnect = http_client_disconnect;
    self->get = http_client_get;
    self->post = http_client_post;
    return (Error){ErrorType.NONE, 0};
}

Error http_client_init(struct HttpClient* self, int transport_type, int protocol_type, HttpResponseMemoryPolicy policy) {
    TransportInterface* transport = NULL;
    if (transport_type == HttpTransportType.TCP) {
        transport = tcp_transport_new(NULL);
    } else if (transport_type == HttpTransportType.UNIX) {
        transport = unix_transport_new(NULL);
    }

    if (!transport) {
        return (Error){ErrorType.TRANSPORT, TransportErrorCode.INIT_FAILURE};
    }

    HttpProtocolInterface* protocol = NULL;
    if (protocol_type == HttpProtocolType.HTTP1) {
        protocol = http1_protocol_new(transport, NULL, policy);
    }

    if (!protocol) {
        // Cleanup the transport if protocol creation failed
        transport->destroy(transport->context);
        return (Error){ErrorType.HTTPC, HttpClientErrorCode.INIT_FAILURE};
    }

    return http_client_init_with_protocol(self, protocol, policy);
}

void http_client_destroy(struct HttpClient* self) {
    if (!self || !self->protocol) {
        return;
    }
    TransportInterface* transport = self->protocol->transport;

    self->protocol->destroy(self->protocol->context);
    transport->destroy(transport->context);
}