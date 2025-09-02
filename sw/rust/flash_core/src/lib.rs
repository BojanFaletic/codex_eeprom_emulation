use anyhow::Result;

#[derive(Clone, Copy, Debug)]
pub struct FlashGeometry {
    pub mem_size: u32,
    pub page_size: u32,
    pub sector_size: u32,
}

pub trait Flash {
    fn geometry(&self) -> FlashGeometry;
    fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<()>;
    fn program(&mut self, addr: u32, data: &[u8]) -> Result<()>;
    fn sector_erase(&mut self, addr: u32) -> Result<()>;
    fn rdsr(&mut self) -> Result<u8> { Ok(0) }
}
