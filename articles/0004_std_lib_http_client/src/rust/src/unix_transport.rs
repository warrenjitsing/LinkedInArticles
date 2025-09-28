use crate::error::{Error, Result, TransportError};
use crate::transport::Transport;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::net::Shutdown;

#[derive(Default)]
pub struct UnixTransport {
    stream: Option<UnixStream>,
}

impl UnixTransport {
    pub fn new() -> Self {
        Self { stream: None }
    }
}

impl Transport for UnixTransport {
    fn connect(&mut self, path: &str, _port: u16) -> Result<()> {
        match UnixStream::connect(path) {
            Ok(stream) => {
                self.stream = Some(stream);
                Ok(())
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                Err(Error::Transport(TransportError::SocketConnectFailure))
            }
            Err(e) => Err(e.into()),
        }
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
    use std::sync::atomic::{AtomicUsize, Ordering};
    use super::*;
    use crate::error::{Error, TransportError};
    use std::io::{Read, Write};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::sync::mpsc;
    use std::thread;
    use std::time::Duration;
    use std::os::unix::io::AsRawFd;

    static TEST_COUNTER: AtomicUsize = AtomicUsize::new(0);

    fn setup_unix_test_server<F>(server_logic: F) -> (String, thread::JoinHandle<()>)
    where
        F: FnOnce(UnixStream) + Send + 'static,
    {
        let count = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
        let socket_path = format!("/tmp/httpc_rust_test_{}_{}", std::process::id(), count);
        let _ = std::fs::remove_file(&socket_path);

        let listener = UnixListener::bind(&socket_path).unwrap();
        let path_clone = socket_path.clone();

        let handle = thread::spawn(move || {
            if let Ok((stream, _)) = listener.accept() {
                server_logic(stream);
            }
            let _ = std::fs::remove_file(&path_clone);
        });

        (socket_path, handle)
    }

    #[test]
    fn construction_succeeds() {
        let transport = UnixTransport::new();
        assert!(transport.stream.is_none());
    }

    #[test]
    fn connect_succeeds() {
        let (path, server_handle) = setup_unix_test_server(|_stream| {});
        let mut transport = UnixTransport::new();
        assert!(transport.connect(&path, 0).is_ok());
        server_handle.join().unwrap();
    }

    #[test]
    fn write_succeeds() {
        let (tx, rx) = mpsc::channel();
        let msg = "hello unix";
        let (path, server_handle) = setup_unix_test_server(move |mut stream| {
            let mut buffer = String::new();
            stream.read_to_string(&mut buffer).unwrap();
            tx.send(buffer).unwrap();
        });

        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();
        transport.write(msg.as_bytes()).unwrap();
        transport.close().unwrap();

        let captured = rx.recv().unwrap();
        assert_eq!(captured, msg);
        server_handle.join().unwrap();
    }

    #[test]
    fn read_succeeds() {
        let msg = "hello from server";
        let (path, server_handle) = setup_unix_test_server(move |mut stream| {
            stream.write_all(msg.as_bytes()).unwrap();
        });

        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();

        let mut buf = [0u8; 1024];
        let bytes_read = transport.read(&mut buf).unwrap();

        assert_eq!(bytes_read, msg.len());
        assert_eq!(&buf[..bytes_read], msg.as_bytes());
        server_handle.join().unwrap();
    }

    #[test]
    fn close_succeeds() {
        let (path, handle) = setup_unix_test_server(|_| {});
        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();
        assert!(transport.close().is_ok());
        handle.join().unwrap();
    }

    #[test]
    fn close_is_idempotent() {
        let (path, handle) = setup_unix_test_server(|_| {});
        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();
        assert!(transport.close().is_ok());
        assert!(transport.close().is_ok());
        handle.join().unwrap();
    }

    #[test]
    fn connect_fails_on_unresponsive_path() {
        let mut transport = UnixTransport::new();
        let path = "/tmp/this-socket-does-not-exist.sock";
        let result = transport.connect(path, 0);
        assert!(result.is_err());
    }

    #[test]
    fn write_fails_on_closed_connection() {
        let (path, handle) = setup_unix_test_server(|stream| {
            let linger = libc::linger { l_onoff: 1, l_linger: 0 };
            unsafe {
                libc::setsockopt(
                    stream.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_LINGER,
                    &linger as *const _ as *const libc::c_void,
                    std::mem::size_of::<libc::linger>() as u32,
                );
            }
        });

        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();
        handle.join().unwrap();
        thread::sleep(Duration::from_millis(50));

        let result = transport.write(b"fail");
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), Error::Transport(TransportError::SocketWriteFailure));
    }

    #[test]
    fn read_fails_on_peer_shutdown() {
        let (path, handle) = setup_unix_test_server(|_| {});
        let mut transport = UnixTransport::new();
        transport.connect(&path, 0).unwrap();
        handle.join().unwrap();

        let mut buf = [0u8; 32];
        let result = transport.read(&mut buf);
        assert!(result.is_err());
        assert_eq!(result.unwrap_err(), Error::Transport(TransportError::ConnectionClosed));
    }
}