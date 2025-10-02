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

    fn xor_checksum(data: &[u8]) -> u64 {
        data.iter().fold(0, |acc, &byte| acc ^ u64::from(byte))
    }

    struct SimpleRng {
        seed: u64,
    }

    impl SimpleRng {
        fn new(seed: u64) -> Self { Self { seed } }
        fn next(&mut self) -> u64 {
            self.seed = self.seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            self.seed
        }
        fn gen_range(&mut self, low: usize, high: usize) -> usize {
            low + (self.next() as usize % (high - low))
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

                #[test]
                fn multi_request_checksum_verification_succeeds() {
                    const NUM_CYCLES: usize = 50;

                    let server_handle = if stringify!($transport_type) == "tcp" {
                        setup_tcp_server(move |mut stream| {
                            let mut server_rng = SimpleRng::new(4321);
                            let mut buffer = vec![0; 4096];
                            let mut bytes_in_buffer = 0;

                            for _ in 0..(NUM_CYCLES * 2) {
                                let mut headers_end = 0;
                                let mut content_length = 0;
                                loop {
                                    let bytes_read = stream.read(&mut buffer[bytes_in_buffer..]).unwrap();
                                    if bytes_read == 0 { return; }
                                    bytes_in_buffer += bytes_read;

                                    if let Some(pos) = buffer[..bytes_in_buffer].windows(4).position(|w| w == b"\r\n\r\n") {
                                        headers_end = pos + 4;
                                        let headers_str = String::from_utf8_lossy(&buffer[..headers_end]);
                                        for line in headers_str.lines() {
                                            if line.to_lowercase().starts_with("content-length:") {
                                                content_length = line[15..].trim().parse::<usize>().unwrap();
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }

                                let total_request_size = headers_end + content_length;
                                while bytes_in_buffer < total_request_size {
                                    let bytes_read = stream.read(&mut buffer[bytes_in_buffer..]).unwrap();
                                    if bytes_read == 0 { break; }
                                    bytes_in_buffer += bytes_read;
                                }

                                let body = &buffer[headers_end..total_request_size];
                                if body.len() >= 16 {
                                    let payload = &body[..body.len() - 16];
                                    let checksum_hex = String::from_utf8_lossy(&body[body.len() - 16..]);
                                    let calculated = xor_checksum(payload);
                                    let received = u64::from_str_radix(&checksum_hex, 16).unwrap();
                                    assert_eq!(calculated, received, "Server-side checksum mismatch");
                                }

                                buffer.copy_within(total_request_size..bytes_in_buffer, 0);
                                bytes_in_buffer -= total_request_size;

                                let body_len = server_rng.gen_range(512, 1024);
                                let res_payload: Vec<u8> = (0..body_len).map(|_| server_rng.next() as u8).collect();
                                let res_checksum = xor_checksum(&res_payload);
                                let res_checksum_hex = format!("{:016x}", res_checksum);

                                let response = format!(
                                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\n\r\n",
                                    res_payload.len() + res_checksum_hex.len()
                                );
                                stream.write_all(response.as_bytes()).unwrap();
                                stream.write_all(&res_payload).unwrap();
                                stream.write_all(res_checksum_hex.as_bytes()).unwrap();
                            }
                        })
                    } else {
                        setup_unix_server(move |mut stream| {
                            let mut server_rng = SimpleRng::new(4321);
                            let mut buffer = vec![0; 4096];
                            let mut bytes_in_buffer = 0;

                            for _ in 0..(NUM_CYCLES * 2) {
                                let mut headers_end = 0;
                                let mut content_length = 0;
                                loop {
                                    let bytes_read = stream.read(&mut buffer[bytes_in_buffer..]).unwrap();
                                    if bytes_read == 0 { return; }
                                    bytes_in_buffer += bytes_read;
                                    if let Some(pos) = buffer[..bytes_in_buffer].windows(4).position(|w| w == b"\r\n\r\n") {
                                        headers_end = pos + 4;
                                        let headers_str = String::from_utf8_lossy(&buffer[..headers_end]);
                                        for line in headers_str.lines() {
                                            if line.to_lowercase().starts_with("content-length:") {
                                                content_length = line[15..].trim().parse::<usize>().unwrap();
                                                break;
                                            }
                                        }
                                        break;
                                    }
                                }
                                let total_request_size = headers_end + content_length;
                                while bytes_in_buffer < total_request_size {
                                    let bytes_read = stream.read(&mut buffer[bytes_in_buffer..]).unwrap();
                                    if bytes_read == 0 { break; }
                                    bytes_in_buffer += bytes_read;
                                }
                                let body = &buffer[headers_end..total_request_size];
                                if body.len() >= 16 {
                                    let payload = &body[..body.len() - 16];
                                    let checksum_hex = String::from_utf8_lossy(&body[body.len() - 16..]);
                                    let calculated = xor_checksum(payload);
                                    let received = u64::from_str_radix(&checksum_hex, 16).unwrap();
                                    assert_eq!(calculated, received, "Server-side checksum mismatch");
                                }
                                buffer.copy_within(total_request_size..bytes_in_buffer, 0);
                                bytes_in_buffer -= total_request_size;
                                let body_len = server_rng.gen_range(512, 1024);
                                let res_payload: Vec<u8> = (0..body_len).map(|_| server_rng.next() as u8).collect();
                                let res_checksum = xor_checksum(&res_payload);
                                let res_checksum_hex = format!("{:016x}", res_checksum);
                                let response = format!("HTTP/1.1 200 OK\r\nContent-Length: {}\r\n\r\n", res_payload.len() + res_checksum_hex.len());
                                stream.write_all(response.as_bytes()).unwrap();
                                stream.write_all(&res_payload).unwrap();
                                stream.write_all(res_checksum_hex.as_bytes()).unwrap();
                            }
                        })
                    };

                    let mut client = HttpClient::<$protocol_struct>::new();
                    client.connect(&server_handle.addr, server_handle.port).unwrap();
                    let mut client_rng = SimpleRng::new(1234);

                    let mut run_client_loop = |use_safe: bool| {
                        for i in 0..NUM_CYCLES {
                            let body_len = client_rng.gen_range(512, 1024);
                            let body: Vec<u8> = (0..body_len).map(|_| client_rng.next() as u8).collect();
                            let checksum = xor_checksum(&body);
                            let checksum_hex = format!("{:016x}", checksum);

                            let mut full_payload = body.clone();
                            full_payload.extend_from_slice(checksum_hex.as_bytes());

                            let content_len_str = full_payload.len().to_string();
                            let mut request = HttpRequest {
                                method: HttpMethod::Get, // Will be overridden by post_* call
                                path: "/",
                                body: &full_payload,
                                headers: vec![HttpHeaderView { key: "Content-Length", value: &content_len_str }],
                            };

                            if use_safe {
                                let res = client.post_safe(&mut request).unwrap();
                                assert_eq!(res.status_code, 200);
                                assert!(res.body.len() >= 16);
                                let payload = &res.body[..res.body.len() - 16];
                                let checksum_hex = String::from_utf8_lossy(&res.body[res.body.len() - 16..]);
                                let calculated = xor_checksum(payload);
                                let received = u64::from_str_radix(&checksum_hex, 16).unwrap();
                                assert_eq!(calculated, received, "Client-side checksum mismatch on iteration (safe) {}", i);
                            } else {
                                let res = client.post_unsafe(&mut request).unwrap();
                                assert_eq!(res.status_code, 200);
                                assert!(res.body.len() >= 16);
                                let payload = &res.body[..res.body.len() - 16];
                                let checksum_hex = String::from_utf8_lossy(&res.body[res.body.len() - 16..]);
                                let calculated = xor_checksum(payload);
                                let received = u64::from_str_radix(&checksum_hex, 16).unwrap();
                                assert_eq!(calculated, received, "Client-side checksum mismatch on iteration (unsafe) {}", i);
                            }
                        }
                    };

                    run_client_loop(true);
                    run_client_loop(false);

                    client.disconnect().unwrap();
                }
            }
        };
    }

    generate_http_client_tests!(tcp, TcpTransport, Http1Protocol<TcpTransport>);
    generate_http_client_tests!(unix, UnixTransport, Http1Protocol<UnixTransport>);
}