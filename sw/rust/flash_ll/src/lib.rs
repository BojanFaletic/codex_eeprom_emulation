use flash_ll_sys as sys;
use flash_core::{Flash, FlashGeometry};

pub struct Driver {
    ctx: sys::FlashLlCtx,
}

impl Driver {
    pub fn new_with_sim(mem_size: u32, page_size: u32, sector_size: u32, sim: *mut sys::AxiSpiSim) -> anyhow::Result<Self> {
        unsafe {
            let cfg = sys::FlashLlConfig {
                base_addr: 0, // unused
                mem_size,
                page_size,
                sector_size,
            };
            let mut ctx = std::mem::MaybeUninit::<sys::FlashLlCtx>::zeroed();
            let ops = sys::flash_ll_axi_sim_ops();
            let rc = sys::flash_ll_init(ctx.as_mut_ptr(), &cfg, ops, sim as *mut _);
            if rc != 0 { anyhow::bail!("flash_ll_init failed: {}", rc); }
            Ok(Driver { ctx: ctx.assume_init() })
        }
    }

    pub fn rdsr(&mut self) -> anyhow::Result<u8> {
        unsafe {
            let mut st: u8 = 0;
            let rc = sys::flash_ll_rdsr(&mut self.ctx, &mut st);
            if rc != 0 { anyhow::bail!("rdsr failed: {}", rc); }
            Ok(st)
        }
    }

    pub fn read(&mut self, addr: u32, buf: &mut [u8]) -> anyhow::Result<()> {
        unsafe {
            let rc = sys::flash_ll_read(&mut self.ctx, addr, buf.as_mut_ptr() as *mut _, buf.len());
            if rc != 0 { anyhow::bail!("read failed: {}", rc); }
            Ok(())
        }
    }

    pub fn program(&mut self, addr: u32, data: &[u8]) -> anyhow::Result<()> {
        unsafe {
            let rc = sys::flash_ll_program(&mut self.ctx, addr, data.as_ptr() as *const _, data.len());
            if rc != 0 { anyhow::bail!("program failed: {}", rc); }
            Ok(())
        }
    }

    pub fn sector_erase(&mut self, addr: u32) -> anyhow::Result<()> {
        unsafe {
            let rc = sys::flash_ll_sector_erase(&mut self.ctx, addr);
            if rc != 0 { anyhow::bail!("sector_erase failed: {}", rc); }
            Ok(())
        }
    }
}

impl Flash for Driver {
    fn geometry(&self) -> FlashGeometry {
        FlashGeometry { mem_size: 0, page_size: 256, sector_size: 4096 }
    }
    fn read(&mut self, addr: u32, buf: &mut [u8]) -> anyhow::Result<()> { self.read(addr, buf) }
    fn program(&mut self, addr: u32, data: &[u8]) -> anyhow::Result<()> { self.program(addr, data) }
    fn sector_erase(&mut self, addr: u32) -> anyhow::Result<()> { self.sector_erase(addr) }
    fn rdsr(&mut self) -> anyhow::Result<u8> { self.rdsr() }
}

#[cfg(feature = "sim")]
pub mod sim {
    use super::*;
    use std::mem;

    pub struct SimEnv {
        pub flash: Box<sys::FlashSim>,
        pub axi: Box<sys::AxiSpiSim>,
    }

    impl SimEnv {
        pub fn new() -> anyhow::Result<Self> {
            unsafe {
                // Allocate on heap to keep stable addresses across moves
                let mut flash: Box<sys::FlashSim> = Box::new(mem::zeroed());
                let cfg = sys::FlashSimConfig {
                    mem_bytes: 8192, // two sectors for EEPROM emulation
                    page_size: 256,
                    sector_size: 4096,
                    prog_busy_ticks: 4,
                    erase_busy_ticks: 64,
                };
                let r = sys::flash_sim_init(&mut *flash, &cfg);
                if r != 0 { anyhow::bail!("flash_sim_init failed: {}", r); }

                let mut axi: Box<sys::AxiSpiSim> = Box::new(mem::zeroed());
                let r2 = sys::axi_spi_sim_init(&mut *axi, &mut *flash, 1024);
                if r2 != 0 { anyhow::bail!("axi_spi_sim_init failed: {}", r2); }

                Ok(SimEnv { flash, axi })
            }
        }
    }

    impl Drop for SimEnv {
        fn drop(&mut self) {
            unsafe {
                sys::axi_spi_sim_free(&mut *self.axi);
                sys::flash_sim_free(&mut *self.flash);
            }
        }
    }

    pub fn driver_with_env(env: &mut SimEnv) -> anyhow::Result<Driver> {
        Driver::new_with_sim(8192, 256, 4096, &mut *env.axi as *mut _)
    }
}
