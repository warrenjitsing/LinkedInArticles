use crate::error::{Error, Result, TransportError};
use crate::transport::Transport;
use std::io::{Read, Write};
use std::net::{Shutdown, TcpStream};
use std::os::unix::io::AsRawFd;
use std::time::Duration;

pub struct TcpTransport {
    stream: Option<TcpStream>,
}

impl TcpTransport {
    pub fn new() -> Self {
        Self { stream: None }
    }
}

impl Transport for TcpTransport {
    fn connect(&mut self, host: &str, port: u16) -> Result<()> {
        let addr = format!("{}:{}", host, port);
        let stream = TcpStream::connect(addr)?;

        stream.set_nodelay(true)?;

        self.stream = Some(stream);
        Ok(())
    }

    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        if let Some(stream) = &mut self.stream {
            let bytes_written = stream.write(buf)?;
            Ok(bytes_written)
        } else {
            Err(Error::Transport(TransportError::SocketWriteFailure))
        }
    }

    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        if let Some(stream) = &mut self.stream {
            let bytes_read = stream.read(buf)?;
            if bytes_read == 0 && !buf.is_empty() {
                return Err(Error::Transport(TransportError::ConnectionClosed));
            }
            Ok(bytes_read)
        } else {
            Err(Error::Transport(TransportError::SocketReadFailure))
        }
    }

    fn close(&mut self) -> Result<()> {
        if let Some(stream) = self.stream.take() {
            stream.shutdown(Shutdown::Both)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::{Error, TransportError};
    use std::io::{Read, Write};
    use std::net::{TcpListener, TcpStream};
    use std::thread;

    fn setup_test_server<F>(server_logic: F) -> (std::net::SocketAddr, thread::JoinHandle<()>)
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

        (local_addr, handle)
    }

    #[test]
    fn construction_succeeds() {
        let transport = TcpTransport::new();
        assert!(transport.stream.is_none());
    }

    #[test]
    fn connect_succeeds() {
        let (addr, server_handle) = setup_test_server(|_stream| {
            // Server logic is empty for a simple connect test.
        });

        let mut transport = TcpTransport::new();
        let result = transport.connect(&addr.ip().to_string(), addr.port());

        assert!(result.is_ok());

        server_handle.join().unwrap();
    }

    #[test]
    fn write_succeeds() {
        let (tx, rx) = std::sync::mpsc::channel();
        let message_to_send = "hello server";

        let (addr, server_handle) = setup_test_server(move |mut stream| {
            let mut buffer = String::new();
            stream.read_to_string(&mut buffer).unwrap();
            tx.send(buffer).unwrap();
        });

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        let bytes_written = transport.write(message_to_send.as_bytes()).unwrap();
        assert_eq!(bytes_written, message_to_send.len());

        transport.close().unwrap();

        let captured_message = rx.recv().unwrap();
        assert_eq!(captured_message, message_to_send);

        server_handle.join().unwrap();
    }

    #[test]
    fn read_succeeds() {
        let message_from_server = "hello client";
        let (addr, server_handle) = setup_test_server(move |mut stream| {
            stream.write_all(message_from_server.as_bytes()).unwrap();
        });

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        let mut read_buffer = [0u8; 1024];
        let bytes_read = transport.read(&mut read_buffer).unwrap();

        assert_eq!(bytes_read, message_from_server.len());
        let received_message = std::str::from_utf8(&read_buffer[..bytes_read]).unwrap();
        assert_eq!(received_message, message_from_server);

        server_handle.join().unwrap();
    }

    #[test]
    fn close_succeeds() {
        let (addr, server_handle) = setup_test_server(|_stream| {});

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        let result = transport.close();
        assert!(result.is_ok());

        server_handle.join().unwrap();
    }

    #[test]
    fn close_is_idempotent() {
        let (addr, server_handle) = setup_test_server(|_stream| {});

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        assert!(transport.close().is_ok());
        assert!(transport.close().is_ok());

        server_handle.join().unwrap();
    }

    #[test]
    fn connect_fails_on_unresponsive_port() {
        let mut transport = TcpTransport::new();
        let unresponsive_port = 65531;

        let result = transport.connect("127.0.0.1", unresponsive_port);

        assert!(result.is_err());
        // We can also assert the specific error type for more precision.
        assert_eq!(
            result.unwrap_err(),
            Error::Transport(TransportError::SocketConnectFailure)
        );
    }


    #[test]
    fn connect_fails_on_dns_failure() {
        let mut transport = TcpTransport::new();
        let invalid_host = "this-is-not-a-real-domain.invalid";

        let result = transport.connect(invalid_host, 80);

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err(),
            Error::Transport(TransportError::DnsFailure)
        );
    }

    #[test]
    fn write_fails_on_closed_connection() {
        let (addr, server_handle) = setup_test_server(|stream| {
            let linger = libc::linger {
                l_onoff: 1, // On
                l_linger: 0, // Timeout of 0
            };
            let fd = stream.as_raw_fd();
            unsafe {
                libc::setsockopt(
                    fd,
                    libc::SOL_SOCKET,
                    libc::SO_LINGER,
                    &linger as *const _ as *const libc::c_void,
                    std::mem::size_of::<libc::linger>() as u32,
                );
            }
        });

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        server_handle.join().unwrap();

        thread::sleep(Duration::from_millis(50));

        let result = transport.write(b"this should fail");

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err(),
            Error::Transport(TransportError::ConnectionClosed)
        );
    }

    #[test]
    fn read_fails_on_peer_shutdown() {
        let (addr, server_handle) = setup_test_server(|_stream| {
            // The stream is dropped, and the connection closed, when this ends.
        });

        let mut transport = TcpTransport::new();
        transport.connect(&addr.ip().to_string(), addr.port()).unwrap();

        server_handle.join().unwrap();

        let mut read_buffer = [0u8; 1024];
        let result = transport.read(&mut read_buffer);

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err(),
            Error::Transport(TransportError::ConnectionClosed)
        );
    }
}