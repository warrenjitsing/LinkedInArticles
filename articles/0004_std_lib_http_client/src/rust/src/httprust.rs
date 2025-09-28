use crate::error::{Error, HttpClientError, Result};
use crate::http_protocol::{
    HttpProtocol, HttpMethod, HttpRequest, SafeHttpResponse, UnsafeHttpResponse,
};
use crate::transport::Transport;
use std::default::Default;

pub struct HttpClient<P: HttpProtocol>
{
    protocol: P,
}

impl<P: HttpProtocol + Default> HttpClient<P>
{
    pub fn new() -> Self {
        Self {
            protocol: P::default(),
        }
    }
}

impl<P: HttpProtocol> HttpClient<P>
{
    pub fn connect(&mut self, host: &str, port: u16) -> Result<()> {
        self.protocol.connect(host, port)
    }

    pub fn disconnect(&mut self) -> Result<()> {
        self.protocol.disconnect()
    }

    pub fn get_safe(&mut self, request: &mut HttpRequest) -> Result<SafeHttpResponse> {
        if !request.body.is_empty() {
            return Err(Error::Http(HttpClientError::InvalidRequest));
        }
        request.method = HttpMethod::Get;
        self.protocol.perform_request_safe(request)
    }

    pub fn get_unsafe<'a>(
        &'a mut self,
        request: &'a mut HttpRequest,
    ) -> Result<UnsafeHttpResponse<'a>> {
        if !request.body.is_empty() {
            return Err(Error::Http(HttpClientError::InvalidRequest));
        }
        request.method = HttpMethod::Get;
        self.protocol.perform_request_unsafe(request)
    }

    pub fn post_safe(&mut self, request: &mut HttpRequest) -> Result<SafeHttpResponse> {
        self.validate_post_request(request)?;
        request.method = HttpMethod::Post;
        self.protocol.perform_request_safe(request)
    }

    pub fn post_unsafe<'a>(
        &'a mut self,
        request: &'a mut HttpRequest,
    ) -> Result<UnsafeHttpResponse<'a>> {
        self.validate_post_request(request)?;
        request.method = HttpMethod::Post;
        self.protocol.perform_request_unsafe(request)
    }

    fn validate_post_request(&self, request: &HttpRequest) -> Result<()> {
        if request.body.is_empty() {
            return Err(Error::Http(HttpClientError::InvalidRequest));
        }

        let content_length_found = request
            .headers
            .iter()
            .any(|h| h.key.eq_ignore_ascii_case("Content-Length"));

        if !content_length_found {
            return Err(Error::Http(HttpClientError::InvalidRequest));
        }

        Ok(())
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use crate::http1_protocol::Http1Protocol;
    use crate::tcp_transport::TcpTransport;
    use crate::unix_transport::UnixTransport;
    use std::io::{Read, Write};
    use std::net::{Shutdown, TcpListener, TcpStream};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;
    use std::sync::mpsc;

    struct ServerHandle {
        _thread: thread::JoinHandle<()>,
        addr: String,
        port: u16,
    }

    fn setup_tcp_server<F>(server_logic: F) -> ServerHandle
    where
        F: FnOnce(TcpStream) + Send + 'static,
    {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let local_addr = listener.local_addr().unwrap();

        let handle = thread::spawn(move || {
            if let Ok((stream, _)) = listener.accept() {
                server_logic(stream);
            }
        });

        ServerHandle {
            _thread: handle,
            addr: local_addr.ip().to_string(),
            port: local_addr.port(),
        }
    }

    static TEST_COUNTER: AtomicUsize = AtomicUsize::new(0);

    fn setup_unix_server<F>(server_logic: F) -> ServerHandle
    where
        F: FnOnce(UnixStream) + Send + 'static,
    {
        let count = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
        let socket_path = format!("/tmp/httprust_client_test_{}_{}", std::process::id(), count);
        let _ = std::fs::remove_file(&socket_path);
        let listener = UnixListener::bind(&socket_path).unwrap();

        let path_for_thread = socket_path.clone();

        let handle = thread::spawn(move || {
            if let Ok((stream, _)) = listener.accept() {
                server_logic(stream);
            }
            let _ = std::fs::remove_file(&path_for_thread);
        });

        ServerHandle {
            _thread: handle,
            addr: socket_path,
            port: 0,
        }
    }

    macro_rules! generate_http_client_tests {
        ($transport_type:ident, $transport_struct:ty, $protocol_struct:ty) => {
            mod $transport_type {
                use super::*;
                use crate::http_protocol::{HttpMethod, HttpRequest, HttpHeaderView};

                #[test]
                fn get_request_safe_succeeds() {
                    let canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
                    let (tx, rx) = mpsc::channel();

                    let server_handle = if stringify!($transport_type) == "tcp" {
                        setup_tcp_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    } else {
                        setup_unix_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    };

                    let mut client = HttpClient::<$protocol_struct>::new();
                    client.connect(&server_handle.addr, server_handle.port).unwrap();

                    let mut request = HttpRequest {
                        method: HttpMethod::Get,
                        path: "/test",
                        body: &[],
                        headers: vec![],
                    };

                    let result = client.get_safe(&mut request);
                    assert!(result.is_ok());
                    let res = result.unwrap();

                    assert_eq!(res.status_code, 200);
                    assert_eq!(res.body, b"success");

                    let captured_request = rx.recv().unwrap();
                    assert!(String::from_utf8_lossy(&captured_request).contains("GET /test HTTP/1.1"));

                    client.disconnect().unwrap();
                }

                #[test]
                fn get_request_unsafe_succeeds() {
                    let canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
                    let (tx, rx) = mpsc::channel();

                    let server_handle = if stringify!($transport_type) == "tcp" {
                        setup_tcp_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    } else {
                        setup_unix_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    };

                    let mut client = HttpClient::<$protocol_struct>::new();
                    client.connect(&server_handle.addr, server_handle.port).unwrap();

                    let mut request = HttpRequest {
                        method: HttpMethod::Get,
                        path: "/test",
                        body: &[],
                        headers: vec![],
                    };

                    let result = client.get_unsafe(&mut request);
                    assert!(result.is_ok());
                    let res = result.unwrap();

                    assert_eq!(res.status_code, 200);
                    assert_eq!(res.body, b"success");

                    let captured_request = rx.recv().unwrap();
                    assert!(String::from_utf8_lossy(&captured_request).contains("GET /test HTTP/1.1"));

                    client.disconnect().unwrap();
                }

                #[test]
                fn post_request_safe_succeeds() {
                    let canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
                    let (tx, rx) = mpsc::channel();

                    let server_handle = if stringify!($transport_type) == "tcp" {
                        setup_tcp_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    } else {
                        setup_unix_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    };

                    let mut client = HttpClient::<$protocol_struct>::new();
                    client.connect(&server_handle.addr, server_handle.port).unwrap();

                    let body_content = b"key=value";
                    let content_len_str = body_content.len().to_string();
                    let mut request = HttpRequest {
                        method: HttpMethod::Get,
                        path: "/submit",
                        body: body_content,
                        headers: vec![
                            HttpHeaderView { key: "Content-Length", value: &content_len_str }
                        ],
                    };

                    let result = client.post_safe(&mut request);
                    assert!(result.is_ok());
                    let res = result.unwrap();

                    assert_eq!(res.status_code, 200);
                    assert_eq!(res.body, b"success");

                    let captured_request = rx.recv().unwrap();
                    let captured_str = String::from_utf8_lossy(&captured_request);
                    assert!(captured_str.contains("POST /submit HTTP/1.1"));
                    assert!(captured_str.ends_with("key=value"));

                    client.disconnect().unwrap();
                }

                #[test]
                fn post_request_unsafe_succeeds() {
                    let canned_response = b"HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nsuccess";
                    let (tx, rx) = mpsc::channel();

                    let server_handle = if stringify!($transport_type) == "tcp" {
                        setup_tcp_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    } else {
                        setup_unix_server(move |mut stream| {
                            let mut buffer = vec![0; 1024];
                            let bytes_read = stream.read(&mut buffer).unwrap();
                            tx.send(buffer[..bytes_read].to_vec()).unwrap();
                            stream.write_all(canned_response).unwrap();
                        })
                    };

                    let mut client = HttpClient::<$protocol_struct>::new();
                    client.connect(&server_handle.addr, server_handle.port).unwrap();

                    let body_content = b"key=value";
                    let content_len_str = body_content.len().to_string();
                    let mut request = HttpRequest {
                        method: HttpMethod::Get,
                        path: "/submit",
                        body: body_content,
                        headers: vec![
                            HttpHeaderView { key: "Content-Length", value: &content_len_str }
                        ],
                    };

                    let result = client.post_unsafe(&mut request);
                    assert!(result.is_ok());
                    let res = result.unwrap();

                    assert_eq!(res.status_code, 200);
                    assert_eq!(res.body, b"success");

                    let captured_request = rx.recv().unwrap();
                    let captured_str = String::from_utf8_lossy(&captured_request);
                    assert!(captured_str.contains("POST /submit HTTP/1.1"));
                    assert!(captured_str.ends_with("key=value"));

                    client.disconnect().unwrap();
                }

                #[test]
                fn get_request_with_body_returns_error() {
                    let mut client = HttpClient::<$protocol_struct>::new();

                    let mut request = HttpRequest {
                        method: HttpMethod::Get,
                        path: "/test",
                        body: b"this body is not allowed",
                        headers: vec![],
                    };

                    let result_safe = client.get_safe(&mut request);
                    assert!(result_safe.is_err());
                    assert_eq!(
                        result_safe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );

                    let result_unsafe = client.get_unsafe(&mut request);
                    assert!(result_unsafe.is_err());
                    assert_eq!(
                        result_unsafe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );
                }

                #[test]
                fn post_request_without_body_returns_error() {
                    let mut client = HttpClient::<$protocol_struct>::new();

                    let mut request = HttpRequest {
                        method: HttpMethod::Post,
                        path: "/test",
                        body: b"",
                        headers: vec![
                            HttpHeaderView { key: "Content-Length", value: "0" }
                        ],
                    };

                    let result_safe = client.post_safe(&mut request);
                    assert!(result_safe.is_err());
                    assert_eq!(
                        result_safe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );

                    let result_unsafe = client.post_unsafe(&mut request);
                    assert!(result_unsafe.is_err());
                    assert_eq!(
                        result_unsafe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );
                }

                #[test]
                fn post_request_without_content_length_returns_error() {
                    let mut client = HttpClient::<$protocol_struct>::new();

                    let mut request = HttpRequest {
                        method: HttpMethod::Post,
                        path: "/test",
                        body: b"some body",
                        headers: vec![],
                    };

                    let result_safe = client.post_safe(&mut request);
                    assert!(result_safe.is_err());
                    assert_eq!(
                        result_safe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );

                    let result_unsafe = client.post_unsafe(&mut request);
                    assert!(result_unsafe.is_err());
                    assert_eq!(
                        result_unsafe.unwrap_err(),
                        Error::Http(HttpClientError::InvalidRequest)
                    );
                }

            }
        };
    }

    generate_http_client_tests!(tcp, TcpTransport, Http1Protocol<TcpTransport>);
    generate_http_client_tests!(unix, UnixTransport, Http1Protocol<UnixTransport>);
}