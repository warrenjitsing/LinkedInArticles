use crate::error::Result;

pub trait Transport {
    fn connect(&mut self, host: &str, port: u16) -> Result<()>;

    fn write(&mut self, buf: &[u8]) -> Result<usize>;

    fn read(&mut self, buf: &mut [u8]) -> Result<usize>;

    fn close(&mut self) -> Result<()>;
}