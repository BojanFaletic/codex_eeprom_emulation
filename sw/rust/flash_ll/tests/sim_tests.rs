#![cfg(feature = "sim")]
use flash_ll::sim::*;

#[test]
fn drv_rdsr_after_reset() {
    let mut env = SimEnv::new().unwrap();
    let mut drv = driver_with_env(&mut env).unwrap();
    let st = drv.rdsr().unwrap();
    assert_eq!(st & 1, 0); // WIP=0
    assert_eq!((st >> 1) & 1, 0); // WEL=0
}

#[test]
fn drv_pp_and_readback() {
    let mut env = SimEnv::new().unwrap();
    let mut drv = driver_with_env(&mut env).unwrap();
    let addr = 0x10u32;
    let data = [0xDEu8, 0xAD, 0xBE, 0xEF];
    drv.program(addr, &data).unwrap();
    let mut out = [0u8; 4];
    drv.read(addr, &mut out).unwrap();
    assert_eq!(&out, &data);
}

#[test]
fn drv_page_boundary_multi_page() {
    let mut env = SimEnv::new().unwrap();
    let mut drv = driver_with_env(&mut env).unwrap();
    let addr = 0xFEu32;
    let data = [0xAAu8, 0xBB, 0xCC, 0xDD];
    drv.program(addr, &data).unwrap();
    let mut out = [0u8; 4];
    drv.read(addr, &mut out).unwrap();
    assert_eq!(&out, &data);
}

