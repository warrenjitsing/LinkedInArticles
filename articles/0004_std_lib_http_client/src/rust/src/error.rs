use std::str::Utf8Error;
use std::num::ParseIntError;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, PartialEq)]
pub enum TransportError {
    DnsFailure,
    SocketCreateFailure,
    SocketConnectFailure,
    SocketWriteFailure,
    SocketReadFailure,
    ConnectionClosed,
    SocketCloseFailure,
    InitFailure,
}

#[derive(Debug, PartialEq)]
pub enum HttpClientError {
    UrlParseFailure,
    HttpParseFailure,
    InitFailure,
}

#[derive(Debug, PartialEq)]
pub enum Error {
    Transport(TransportError),
    Http(HttpClientError),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        eprintln!("\nCaught underlying std::io::Error: {:?}\n", err);
        let kind = match err.kind() {
            std::io::ErrorKind::NotFound => TransportError::DnsFailure,
            std::io::ErrorKind::ConnectionRefused => TransportError::SocketConnectFailure,
            std::io::ErrorKind::ConnectionReset => TransportError::ConnectionClosed,
            std::io::ErrorKind::BrokenPipe => TransportError::SocketWriteFailure,
            _ if err.to_string().contains("Name or service not known") => {
                TransportError::DnsFailure
            }
            _ => TransportError::SocketReadFailure,
        };
        Error::Transport(kind)
    }
}

impl From<Utf8Error> for Error {
    fn from(_: Utf8Error) -> Self {
        Error::Http(HttpClientError::HttpParseFailure)
    }
}

impl From<ParseIntError> for Error {
    fn from(_: ParseIntError) -> Self {
        Error::Http(HttpClientError::HttpParseFailure)
    }
}