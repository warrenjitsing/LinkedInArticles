use crate::error::Result;
use crate::transport::Transport;

#[derive(Debug, PartialEq)]
pub enum HttpMethod {
    Get,
    Post,
}

#[derive(Debug, PartialEq, Clone)]
pub struct HttpHeaderView<'a> {
    pub key: &'a str,
    pub value: &'a str,
}

#[derive(Debug, PartialEq, Clone)]
pub struct HttpOwnedHeader {
    pub key: String,
    pub value: String,
}

#[derive(Debug, PartialEq)]
pub struct HttpRequest<'a> {
    pub method: HttpMethod,
    pub path: &'a str,
    pub body: &'a [u8],
    pub headers: Vec<HttpHeaderView<'a>>,
}

#[derive(Debug, PartialEq)]
pub struct SafeHttpResponse {
    pub status_code: u16,
    pub status_message: String,
    pub body: Vec<u8>,
    pub headers: Vec<HttpOwnedHeader>,
}

#[derive(Debug, PartialEq)]
pub struct UnsafeHttpResponse<'a> {
    pub status_code: u16,
    pub status_message: &'a str,
    pub body: &'a [u8],
    pub headers: Vec<HttpHeaderView<'a>>,
}

pub trait ParsableResponse<'a>: Sized {
    fn from_parts(
        status_code: u16,
        status_message: &'a str,
        headers: Vec<HttpHeaderView<'a>>,
        body: &'a [u8],
    ) -> Result<Self>;
}

impl<'a> ParsableResponse<'a> for SafeHttpResponse {
    fn from_parts(
        status_code: u16,
        status_message: &'a str,
        headers: Vec<HttpHeaderView<'a>>,
        body: &'a [u8],
    ) -> Result<Self> {
        Ok(SafeHttpResponse {
            status_code,
            status_message: status_message.to_string(),
            headers: headers
                .iter()
                .map(|h| HttpOwnedHeader {
                    key: h.key.to_string(),
                    value: h.value.to_string(),
                })
                .collect(),
            body: body.to_vec(),
        })
    }
}

impl<'a> ParsableResponse<'a> for UnsafeHttpResponse<'a> {
    fn from_parts(
        status_code: u16,
        status_message: &'a str,
        headers: Vec<HttpHeaderView<'a>>,
        body: &'a [u8],
    ) -> Result<Self> {
        Ok(UnsafeHttpResponse {
            status_code,
            status_message,
            headers,
            body,
        })
    }
}

pub trait HttpProtocol<T: Transport> {
    fn connect(&mut self, host: &str, port: u16) -> Result<()>;

    fn disconnect(&mut self) -> Result<()>;

    fn perform_request_unsafe<'a, 'b>(&'a mut self, request: &'b HttpRequest) -> Result<UnsafeHttpResponse<'a>>;

    fn perform_request_safe<'a>(&mut self, request: &'a HttpRequest) -> Result<SafeHttpResponse>;
}