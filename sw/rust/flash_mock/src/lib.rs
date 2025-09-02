use anyhow::Result;
use flash_core::{Flash, FlashGeometry};

pub struct MockFlash {
    geom: FlashGeometry,
    mem: Vec<u8>,
}

impl MockFlash {
    pub fn new(mem_size: u32, page_size: u32, sector_size: u32) -> Self {
        let geom = FlashGeometry { mem_size, page_size, sector_size };
        Self { geom, mem: vec![0xFF; mem_size as usize] }
    }
}

impl Flash for MockFlash {
    fn geometry(&self) -> FlashGeometry { self.geom }

    fn read(&mut self, addr: u32, buf: &mut [u8]) -> Result<()> {
        let end = addr as usize + buf.len();
        if end > self.mem.len() { anyhow::bail!("oob"); }
        buf.copy_from_slice(&self.mem[addr as usize..end]);
        Ok(())
    }

    fn program(&mut self, addr: u32, data: &[u8]) -> Result<()> {
        // Respect page boundary: split if needed
        let mut a = addr as usize;
        let mut off = 0usize;
        while off < data.len() {
            let page_off = a % self.geom.page_size as usize;
            let room = self.geom.page_size as usize - page_off;
            let chunk = room.min(data.len() - off);
            let end = a + chunk;
            if end > self.mem.len() { anyhow::bail!("oob"); }
            for i in 0..chunk { self.mem[a + i] &= data[off + i]; }
            a += chunk; off += chunk;
        }
        Ok(())
    }

    fn sector_erase(&mut self, addr: u32) -> Result<()> {
        let base = ((addr as usize) / self.geom.sector_size as usize) * self.geom.sector_size as usize;
        let end = (base + self.geom.sector_size as usize).min(self.mem.len());
        for b in &mut self.mem[base..end] { *b = 0xFF; }
        Ok(())
    }
}

