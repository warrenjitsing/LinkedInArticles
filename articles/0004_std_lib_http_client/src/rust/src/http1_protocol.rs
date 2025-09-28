use std::io::Write;
use std::cmp::max;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::default::Default;

use crate::error::{Error, HttpClientError, Result, TransportError};
use crate::http_protocol::{HttpHeaderView, HttpOwnedHeader, HttpMethod, HttpProtocol, HttpRequest, SafeHttpResponse, UnsafeHttpResponse};
use crate::transport::Transport;

static TEST_COUNTER: AtomicUsize = AtomicUsize::new(0);

pub struct Http1Protocol<T: Transport> {
    transport: T,
    buffer: Vec<u8>,
    header_size: usize,
    content_length: Option<usize>,
}

impl<T: Transport + Default> Default for Http1Protocol<T> {
    fn default() -> Self {
        Self {
            transport: T::default(),
            buffer: Vec::new(), // or Vec::default()
            header_size: 0,
            content_length: None,
        }
    }
}

impl<T: Transport> Http1Protocol<T> {
    const HEADER_SEPARATOR: &'static [u8] = b"\r\n\r\n";
    const HEADER_SEPARATOR_CL: &'static [u8] = b"Content-Length:";

    pub fn new(transport: T) -> Self {
        Self {
            transport,
            buffer: Vec::with_capacity(1024),
            header_size: 0,
            content_length: None,
        }
    }

    // --- Private Helper Methods ---

    fn build_request_string(&mut self, request: &HttpRequest) {
        self.buffer.clear();

        let method_str = match request.method {
            HttpMethod::Get => "GET",
            HttpMethod::Post => "POST",
        };

        write!(&mut self.buffer, "{} {} HTTP/1.1\r\n", method_str, request.path).unwrap();

        for header in &request.headers {
            write!(&mut self.buffer, "{}: {}\r\n", header.key, header.value).unwrap();
        }

        self.buffer.extend_from_slice(b"\r\n");

        if !request.body.is_empty() && request.method == HttpMethod::Post {
            self.buffer.extend_from_slice(request.body);
        }
    }

    fn read_full_response(&mut self) -> Result<()> {
        self.buffer.clear();
        self.header_size = 0;
        self.content_length = None;

        loop {
            let available_capacity = self.buffer.capacity() - self.buffer.len();
            let read_amount = max(available_capacity, 1024);
            let old_len = self.buffer.len();
            self.buffer.resize(old_len + read_amount, 0);

            let bytes_read = match self.transport.read(&mut self.buffer[old_len..]) {
                Ok(n) => n,
                Err(Error::Transport(TransportError::ConnectionClosed)) => {
                    self.buffer.truncate(old_len);
                    if self.content_length.is_some() && self.buffer.len() < self.header_size + self.content_length.unwrap() {
                        return Err(Error::Http(HttpClientError::HttpParseFailure));
                    }
                    break;
                }
                Err(e) => {
                    self.buffer.truncate(old_len);
                    return Err(e);
                }
            };

            self.buffer.truncate(old_len + bytes_read);

            if self.header_size == 0 {
                if let Some(pos) = self.buffer.windows(4).position(|window| window == Self::HEADER_SEPARATOR) {
                    self.header_size = pos + 4;
                    let headers_view = &self.buffer[..self.header_size];

                    for line in headers_view.split(|&b| b == b'\n').skip(1) {
                        let line = if line.ends_with(b"\r") { &line[..line.len() - 1] } else { line };
                        if line.is_empty() { break; }

                        if line.len() >= 15 && line[..15].eq_ignore_ascii_case(Self::HEADER_SEPARATOR_CL) {
                            if let Some(colon_pos) = line.iter().position(|&b| b == b':') {
                                let value_slice = &line[colon_pos + 1..];
                                if let Some(start) = value_slice.iter().position(|&b| !b.is_ascii_whitespace()) {
                                    if let Ok(s) = std::str::from_utf8(&value_slice[start..]) {
                                        if let Ok(len) = s.parse::<usize>() {
                                            self.content_length = Some(len);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if let Some(content_len) = self.content_length {
                if self.buffer.len() >= self.header_size + content_len {
                    break;
                }
            }
        }

        if self.header_size == 0 && !self.buffer.is_empty() {
            return Err(Error::Http(HttpClientError::HttpParseFailure));
        }

        Ok(())
    }

    fn parse_unsafe_response<'a>(&'a self) -> Result<UnsafeHttpResponse<'a>> {
        if self.header_size == 0 {
            return Err(Error::Http(HttpClientError::HttpParseFailure));
        }

        let headers_block = &self.buffer[..self.header_size - Self::HEADER_SEPARATOR.len()];

        let mut parts = headers_block.splitn(2, |&b| b == b'\n');
        let status_line_bytes = parts.next().unwrap_or_default();
        let rest_of_headers_bytes = parts.next().unwrap_or_default();

        let status_line_str = std::str::from_utf8(status_line_bytes)?.trim_end();
        let mut status_parts = status_line_str.splitn(3, ' ');

        let _http_version = status_parts.next();
        let status_code_str = status_parts.next().ok_or(Error::Http(HttpClientError::HttpParseFailure))?;
        let status_message = status_parts.next().unwrap_or("");
        let status_code = status_code_str.parse::<u16>()?;

        let headers = rest_of_headers_bytes
            .split(|&b| b == b'\n')
            .filter_map(|line| {
                let line = if line.ends_with(b"\r") { &line[..line.len() - 1] } else { line };
                if line.is_empty() { return None; }

                let mut parts = line.splitn(2, |&b| b == b':');
                let key_bytes = parts.next()?;
                let value_bytes = parts.next()?;

                let key = std::str::from_utf8(key_bytes).ok()?;
                let value = std::str::from_utf8(value_bytes).ok()?.trim();

                Some(HttpHeaderView { key, value })
            })
            .collect();

        let body = if let Some(len) = self.content_length {
            &self.buffer[self.header_size..self.header_size + len]
        } else {
            &self.buffer[self.header_size..]
        };

        Ok(UnsafeHttpResponse {
            status_code,
            status_message,
            headers,
            body,
            content_length: self.content_length,
        })
    }

    #[allow(dead_code)] // To silence warnings until we use it in all tests
    pub fn get_content_length_for_test(&self) -> Option<usize> {
        self.content_length
    }

    #[allow(dead_code)]
    pub fn get_internal_buffer_ptr_for_test(&self) -> *const u8 {
        self.buffer.as_ptr()
    }

}

impl<T: Transport> HttpProtocol for Http1Protocol<T> {
    type Transport = T;
    fn connect(&mut self, host: &str, port: u16) -> Result<()> {
        self.transport.connect(host, port)
    }

    fn disconnect(&mut self) -> Result<()> {
        self.transport.close()
    }

    fn perform_request_unsafe<'a, 'b>(&'a mut self, request: &'b HttpRequest) -> Result<UnsafeHttpResponse<'a>> {
        self.build_request_string(request);
        self.transport.write(&self.buffer)?;
        self.read_full_response()?;
        self.parse_unsafe_response()
    }

    fn perform_request_safe<'a>(&mut self, request: &'a HttpRequest) -> Result<SafeHttpResponse> {
        let unsafe_res = self.perform_request_unsafe(request)?;

        let headers = unsafe_res.headers
            .iter()
            .map(|h| HttpOwnedHeader {
                key: h.key.to_string(),
                value: h.value.to_string(),
            })
            .collect();

        Ok(SafeHttpResponse {
            status_code: unsafe_res.status_code,
            status_message: unsafe_res.status_message.to_string(),
            body: unsafe_res.body.to_vec(),
            headers,
            content_length: unsafe_res.content_length,
        })
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use std::net::{TcpListener, Shutdown};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::io::{Read, Write};
    use std::thread;
    use std::sync::mpsc;

    use crate::transport::Transport;
    use crate::tcp_transport::TcpTransport;
    use crate::unix_transport::UnixTransport;

    macro_rules! generate_http1_protocol_tests {
        ($transport_type:ty, $server_logic:expr) => {
            #[test]
            fn connect_and_disconnect_succeeds() {
                let server_handle = $server_logic(|_stream| {});
                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                assert!(protocol.connect(&server_handle.addr, server_handle.port).is_ok());
                assert!(protocol.disconnect().is_ok());
            }

            #[test]
            fn perform_request_fails_if_not_connected() {
                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_err());
                assert!(matches!(
                    result.unwrap_err(),
                    Error::Transport(TransportError::SocketWriteFailure)
                ));
            }

            #[test]
            fn correctly_serializes_get_request() {
                let (tx, rx) = mpsc::channel();

                let server_handle = $server_logic(move |mut stream| {
                    let mut buffer = vec![0; 1024];
                    let bytes_read = stream.read(&mut buffer).unwrap();
                    tx.send(buffer[..bytes_read].to_vec()).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/test",
                    body: &[],
                    headers: vec![HttpHeaderView { key: "Host", value: "example.com" }],
                };

                let _ = protocol.perform_request_unsafe(&request);

                let captured_request = rx.recv().unwrap();

                let expected_request = b"GET /test HTTP/1.1\r\nHost: example.com\r\n\r\n";

                assert_eq!(captured_request, expected_request);
            }

            #[test]
            fn correctly_serializes_post_request() {
                let (tx, rx) = mpsc::channel();

                let server_handle = $server_logic(move |mut stream| {
                    let mut buffer = vec![0; 1024];
                    let bytes_read = stream.read(&mut buffer).unwrap();
                    tx.send(buffer[..bytes_read].to_vec()).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let body = b"key=value";
                let request = HttpRequest {
                    method: HttpMethod::Post,
                    path: "/api/submit",
                    body,
                    headers: vec![
                        HttpHeaderView { key: "Host", value: "test-server" },
                        HttpHeaderView { key: "Content-Length", value: "9" },
                    ],
                };

                let _ = protocol.perform_request_unsafe(&request);

                let captured_request = rx.recv().unwrap();

                let expected_request =
                    b"POST /api/submit HTTP/1.1\r\n\
                      Host: test-server\r\n\
                      Content-Length: 9\r\n\
                      \r\n\
                      key=value";

                assert_eq!(captured_request, expected_request);
            }

            #[test]
            fn successfully_parses_response_with_content_length() {
                let canned_response = b"HTTP/1.1 200 OK\r\n\
                                       Content-Type: text/plain\r\n\
                                       Content-Length: 12\r\n\
                                       \r\n\
                                       Hello Client";

                let server_handle = $server_logic(|mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 200);
                assert_eq!(res.status_message, "OK");
                assert_eq!(res.headers.len(), 2);
                assert_eq!(res.headers[0].key, "Content-Type");
                assert_eq!(res.headers[0].value, "text/plain");
                assert_eq!(res.headers[1].key, "Content-Length");
                assert_eq!(res.headers[1].value, "12");
                assert_eq!(res.body, b"Hello Client");
            }

            #[test]
            fn successfully_reads_body_on_connection_close() {
                let canned_response = b"HTTP/1.1 200 OK\r\n\
                                       Connection: close\r\n\
                                       \r\n\
                                       Full body.";

                let server_handle = $server_logic(|mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 200);
                assert_eq!(res.body, b"Full body.");

                assert_eq!(protocol.get_content_length_for_test(), None);
            }

            #[test]
            fn correctly_parses_complex_status_line_and_headers() {
                let response_body = b"{\"error\":\"not found\"}";
                let canned_response = format!(
                    "HTTP/1.1 404 Not Found\r\n\
                     Connection: close\r\n\
                     Content-Type: application/json\r\n\
                     X-Request-ID: abc-123\r\n\
                     Content-Length: {}\r\n\
                     \r\n",
                    response_body.len()
                );

                let server_handle = $server_logic(move |mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response.as_bytes()).unwrap();
                    stream.write_all(response_body).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 404);
                assert_eq!(res.status_message, "Not Found");

                assert_eq!(res.headers.len(), 4);
                assert_eq!(res.headers[0].key, "Connection");
                assert_eq!(res.headers[0].value, "close");
                assert_eq!(res.headers[1].key, "Content-Type");
                assert_eq!(res.headers[1].value, "application/json");
                assert_eq!(res.headers[2].key, "X-Request-ID");
                assert_eq!(res.headers[2].value, "abc-123");
                assert_eq!(res.headers[3].key, "Content-Length");
                assert_eq!(res.headers[3].value, "21");

                assert_eq!(res.body, response_body);
            }

            #[test]
            fn handles_zero_content_length_response() {
                let canned_response = b"HTTP/1.1 204 No Content\r\n\
                                       Connection: close\r\n\
                                       Content-Length: 0\r\n\
                                       \r\n";

                let server_handle = $server_logic(|mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 204);
                assert_eq!(res.headers.len(), 2);
                assert_eq!(res.headers[1].key, "Content-Length");
                assert_eq!(res.headers[1].value, "0");
                assert!(res.body.is_empty());
            }

            #[test]
            fn handles_response_larger_than_initial_buffer() {
                let large_body = vec![b'a'; 2000];
                let body_for_server = large_body.clone();
                let canned_response_headers = format!(
                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\n\r\n",
                    large_body.len()
                );

                let server_handle = $server_logic(move |mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();

                    stream.write_all(canned_response_headers.as_bytes()).unwrap();
                    stream.write_all(&body_for_server).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 200);
                assert_eq!(res.body.len(), large_body.len());
                assert_eq!(res.body, large_body.as_slice());
            }

            #[test]
            fn fails_gracefully_on_bad_content_length() {
                let response_body = b"short body";
                let canned_response_headers = format!(
                    "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n"
                );

                let server_handle = $server_logic(move |mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response_headers.as_bytes()).unwrap();
                    stream.write_all(response_body).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_unsafe(&request);

                assert!(result.is_err());
                assert!(matches!(
                    result.unwrap_err(),
                    Error::Http(HttpClientError::HttpParseFailure)
                ));
            }

            #[test]
            fn safe_request_returns_owning_deep_copy() {
                let canned_response = b"HTTP/1.1 200 OK\r\n\
                                       Content-Length: 11\r\n\
                                       \r\n\
                                       Safe Buffer";

                let server_handle = $server_logic(|mut stream| {
                    let mut buffer = vec![0; 1024];
                    stream.read(&mut buffer).unwrap();
                    stream.write_all(canned_response).unwrap();
                    stream.shutdown(Shutdown::Write).unwrap();
                });

                let mut protocol = Http1Protocol::new(<$transport_type>::new());
                protocol.connect(&server_handle.addr, server_handle.port).unwrap();

                let request = HttpRequest {
                    method: HttpMethod::Get,
                    path: "/",
                    body: &[],
                    headers: vec![],
                };

                let result = protocol.perform_request_safe(&request);

                assert!(result.is_ok());
                let res = result.unwrap();

                assert_eq!(res.status_code, 200);
                assert_eq!(res.body, b"Safe Buffer");

                assert_ne!(
                    res.body.as_ptr(),
                    protocol.get_internal_buffer_ptr_for_test()
                );
            }
        };
    }

    struct ServerHandle {
        _thread: thread::JoinHandle<()>,
        addr: String,
        port: u16,
    }

    mod tcp_tests {
        use super::*;

        fn setup_tcp_server<F>(server_logic: F) -> ServerHandle
        where F: FnOnce(std::net::TcpStream) + Send + 'static {
            let listener = TcpListener::bind("127.0.0.1:0").unwrap();
            let local_addr = listener.local_addr().unwrap();

            let handle = thread::spawn(move || {
                if let Ok((stream, _)) = listener.accept() {
                    server_logic(stream);
                }
            });

            ServerHandle { _thread: handle, addr: local_addr.ip().to_string(), port: local_addr.port() }
        }

        generate_http1_protocol_tests!(TcpTransport, setup_tcp_server);
    }

    mod unix_tests {
        use super::*;

        fn setup_unix_server<F>(server_logic: F) -> ServerHandle
        where F: FnOnce(UnixStream) + Send + 'static {
            let count = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
            let socket_path = format!("/tmp/httpc_rust_test_{}_{}", std::process::id(), count);
            let _ = std::fs::remove_file(&socket_path);
            let listener = UnixListener::bind(&socket_path).unwrap();

            let path_for_thread = socket_path.clone();

            let handle = thread::spawn(move || {
                if let Ok((stream, _)) = listener.accept() {
                    server_logic(stream);
                }
                let _ = std::fs::remove_file(&path_for_thread);
            });

            ServerHandle { _thread: handle, addr: socket_path, port: 0 }
        }

        generate_http1_protocol_tests!(UnixTransport, setup_unix_server);
    }
}