use anyhow::{anyhow, Result};
use flash_core::Flash;

const SECTOR_MAGIC: u32 = 0xEE5EC007; // arbitrary non-FF marker
const REC_MAGIC: u32 = 0xEE4C0A11;    // arbitrary non-FF marker

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct SectorHeader {
    magic: u32,
    seq: u32,
    reserved0: u32,
    reserved1: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RecHeader {
    magic: u32,
    seq: u32,
    addr: u32,
    len: u32,
    crc32: u32,
}

fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &b in bytes {
        let mut x = (crc ^ (b as u32)) & 0xFF;
        for _ in 0..8 {
            let mask = 0u32.wrapping_sub(x & 1);
            x = (x >> 1) ^ (0xEDB8_8320u32 & mask);
        }
        crc = (crc >> 8) ^ x;
    }
    !crc
}

fn pad4(x: usize) -> usize { (x + 3) & !3 }

pub struct Eeprom<F: Flash> {
    flash: F,
    base: u32,
    sector_size: u32,
    size: u32, // logical EEPROM size (bytes)
    active_base: u32,
    scratch_base: u32,
    seq: u32,
    state: Vec<u8>,
    wptr: u32, // write pointer within active sector
}

impl<F: Flash> Eeprom<F> {
    pub fn new_with_flash(flash: F, base: u32, sector_size: u32, size: u32) -> Result<Self> {
        if size == 0 || sector_size == 0 || (size as u64) > (sector_size as u64 * 2) {
            return Err(anyhow!("invalid sizes"));
        }
        let mut ee = Eeprom {
            flash,
            base,
            sector_size,
            size,
            active_base: base,
            scratch_base: base + sector_size,
            seq: 0,
            state: vec![0xFF; size as usize],
            wptr: 0,
        };
        ee.init_or_format()?;
        Ok(ee)
    }

    fn read_exact(&mut self, addr: u32, buf: &mut [u8]) -> Result<()> {
        self.flash.read(addr, buf)
    }

    fn write_all(&mut self, addr: u32, data: &[u8]) -> Result<()> {
        self.flash.program(addr, data)
    }

    fn erase_sector(&mut self, base: u32) -> Result<()> {
        self.flash.sector_erase(base)
    }

    fn init_or_format(&mut self) -> Result<()> {
        // Read both sector headers
        let mut buf = vec![0u8; core::mem::size_of::<SectorHeader>()];
        self.read_exact(self.base, &mut buf)?;
        let sh_a = parse_sector_header(&buf);
        self.read_exact(self.base + self.sector_size, &mut buf)?;
        let sh_b = parse_sector_header(&buf);

        let (active_base, scratch_base, seq) = match (sh_a, sh_b) {
            (Some(a), Some(b)) => {
                if a.seq >= b.seq { (self.base, self.base + self.sector_size, a.seq) }
                else { (self.base + self.sector_size, self.base, b.seq) }
            }
            (Some(a), None) => (self.base, self.base + self.sector_size, a.seq),
            (None, Some(b)) => (self.base + self.sector_size, self.base, b.seq),
            (None, None) => {
                // Format: erase both, write header to A
                self.erase_sector(self.base)?;
                self.erase_sector(self.base + self.sector_size)?;
                let hdr = SectorHeader { magic: SECTOR_MAGIC, seq: 1, reserved0: 0, reserved1: 0 };
                let mut hb = vec![0u8; core::mem::size_of::<SectorHeader>()];
                write_sector_header_bytes(&hdr, &mut hb);
                self.write_all(self.base, &hb)?;
                (self.base, self.base + self.sector_size, 1)
            }
        };

        self.active_base = active_base;
        self.scratch_base = scratch_base;
        self.seq = seq;
        self.replay_log()?;
        Ok(())
    }

    fn replay_log(&mut self) -> Result<()> {
        // start after header
        let mut off = core::mem::size_of::<SectorHeader>() as u32;
        self.state.fill(0xFF);
        while (off as u64) < (self.sector_size as u64) {
            let mut hb = [0u8; core::mem::size_of::<RecHeader>()];
            self.read_exact(self.active_base + off, &mut hb)?;
            match parse_rec_header(&hb) {
                None => { break; }, // hit blank or invalid
                Some(h) => {
                    if h.magic != REC_MAGIC || h.len == 0 { break; }
                    let data_off = off + core::mem::size_of::<RecHeader>() as u32;
                    let mut data = vec![0u8; h.len as usize];
                    self.read_exact(self.active_base + data_off, &mut data)?;
                    let mut check = vec![];
                    check.extend_from_slice(&hb);
                    check.extend_from_slice(&data);
                    if crc32(&check) != h.crc32 { break; }
                    // apply
                    let start = h.addr as usize;
                    let end = (h.addr + h.len) as usize;
                    if end <= self.state.len() {
                        self.state[start..end].copy_from_slice(&data);
                    }
                    let consumed = pad4(core::mem::size_of::<RecHeader>() + h.len as usize) as u32;
                    off = off + consumed;
                }
            }
        }
        self.wptr = off;
        Ok(())
    }

    fn ensure_space(&mut self, need: usize) -> Result<()> {
        let avail = (self.sector_size as usize).saturating_sub(self.wptr as usize);
        if avail >= pad4(need) { return Ok(()); }
        self.compact()
    }

    fn compact(&mut self) -> Result<()> {
        // Write a fresh sector: header(seq+1) + snapshot record of full state
        self.erase_sector(self.scratch_base)?;
        let new_seq = self.seq + 1;
        let sh = SectorHeader { magic: SECTOR_MAGIC, seq: new_seq, reserved0: 0, reserved1: 0 };
        let mut hb = vec![0u8; core::mem::size_of::<SectorHeader>()];
        write_sector_header_bytes(&sh, &mut hb);
        self.write_all(self.scratch_base, &hb)?;

        // snapshot record
        let rec = RecHeader { magic: REC_MAGIC, seq: new_seq, addr: 0, len: self.size, crc32: 0 };
        let mut rhb = [0u8; core::mem::size_of::<RecHeader>()];
        write_rec_header_bytes(&rec, &mut rhb);
        let mut check = vec![]; check.extend_from_slice(&rhb); check.extend_from_slice(&self.state);
        let crc = crc32(&check);
        let rec = RecHeader { crc32: crc, ..rec };
        write_rec_header_bytes(&rec, &mut rhb);
        let data_off = self.scratch_base + core::mem::size_of::<SectorHeader>() as u32 + 0;
        self.write_all(self.scratch_base + core::mem::size_of::<SectorHeader>() as u32, &rhb)?;
        // Avoid overlapping borrows of &mut self and &self.state by cloning snapshot
        let snapshot = self.state.clone();
        self.write_all(data_off + core::mem::size_of::<RecHeader>() as u32, &snapshot)?;

        // swap roles
        core::mem::swap(&mut self.active_base, &mut self.scratch_base);
        self.seq = new_seq;
        self.wptr = core::mem::size_of::<SectorHeader>() as u32 + pad4(core::mem::size_of::<RecHeader>() + self.state.len()) as u32;
        Ok(())
    }

    pub fn read(&self, addr: u32, out: &mut [u8]) -> Result<()> {
        let end = addr as usize + out.len();
        if end > self.state.len() { return Err(anyhow!("oob")); }
        out.copy_from_slice(&self.state[addr as usize..end]);
        Ok(())
    }

    pub fn write(&mut self, addr: u32, data: &[u8]) -> Result<()> {
        let end = addr as usize + data.len();
        if end > self.state.len() { return Err(anyhow!("oob")); }
        // Prepare record
        let mut hdr = RecHeader { magic: REC_MAGIC, seq: self.seq, addr, len: data.len() as u32, crc32: 0 };
        let mut hb = [0u8; core::mem::size_of::<RecHeader>()];
        write_rec_header_bytes(&hdr, &mut hb);
        let mut check = vec![]; check.extend_from_slice(&hb); check.extend_from_slice(data);
        hdr.crc32 = crc32(&check);
        write_rec_header_bytes(&hdr, &mut hb);
        // Ensure space
        let need = pad4(core::mem::size_of::<RecHeader>() + data.len());
        self.ensure_space(need)?;
        // Append to active
        let off = self.active_base + self.wptr;
        self.write_all(off, &hb)?;
        self.write_all(off + core::mem::size_of::<RecHeader>() as u32, data)?;
        // Advance write pointer (4-byte aligned)
        self.wptr += need as u32;
        // Update in-memory state
        self.state[addr as usize..end].copy_from_slice(data);
        Ok(())
    }
}

fn parse_sector_header(buf: &[u8]) -> Option<SectorHeader> {
    if buf.len() < core::mem::size_of::<SectorHeader>() { return None; }
    let magic = u32::from_le_bytes(buf[0..4].try_into().unwrap());
    let seq = u32::from_le_bytes(buf[4..8].try_into().unwrap());
    if magic == SECTOR_MAGIC && seq != 0xFFFF_FFFF { Some(SectorHeader { magic, seq, reserved0: 0, reserved1: 0 }) } else { None }
}

fn write_sector_header_bytes(h: &SectorHeader, out: &mut [u8]) {
    out[0..4].copy_from_slice(&h.magic.to_le_bytes());
    out[4..8].copy_from_slice(&h.seq.to_le_bytes());
    out[8..12].copy_from_slice(&0u32.to_le_bytes());
    out[12..16].copy_from_slice(&0u32.to_le_bytes());
}

fn parse_rec_header(buf: &[u8]) -> Option<RecHeader> {
    if buf.len() < core::mem::size_of::<RecHeader>() { return None; }
    let magic = u32::from_le_bytes(buf[0..4].try_into().unwrap());
    if magic == 0xFFFF_FFFF || magic == 0 { return None; }
    let seq = u32::from_le_bytes(buf[4..8].try_into().unwrap());
    let addr = u32::from_le_bytes(buf[8..12].try_into().unwrap());
    let len = u32::from_le_bytes(buf[12..16].try_into().unwrap());
    let crc32 = u32::from_le_bytes(buf[16..20].try_into().unwrap());
    Some(RecHeader { magic, seq, addr, len, crc32 })
}

fn write_rec_header_bytes(h: &RecHeader, out: &mut [u8]) {
    out[0..4].copy_from_slice(&h.magic.to_le_bytes());
    out[4..8].copy_from_slice(&h.seq.to_le_bytes());
    out[8..12].copy_from_slice(&h.addr.to_le_bytes());
    out[12..16].copy_from_slice(&h.len.to_le_bytes());
    out[16..20].copy_from_slice(&h.crc32.to_le_bytes());
}

#[cfg(feature = "mock")]
pub mod mock {
    use super::*;
    use flash_mock::MockFlash;

    pub fn new_mock(base: u32, sector_size: u32, size: u32) -> Result<Eeprom<MockFlash>> {
        let flash = MockFlash::new(sector_size * 2, 256, sector_size);
        Eeprom::new_with_flash(flash, base, sector_size, size)
    }
}

#[cfg(feature = "ffi")]
pub mod ffi {
    use super::*;
    use flash_ll::Driver; // Driver already implements Flash in flash_ll crate

    pub fn new_with_driver(drv: Driver, base: u32, sector_size: u32, size: u32) -> Result<Eeprom<Driver>> {
        Eeprom::new_with_flash(drv, base, sector_size, size)
    }
}
