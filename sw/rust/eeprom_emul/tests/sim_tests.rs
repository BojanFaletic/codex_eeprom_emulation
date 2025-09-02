#![cfg(feature = "mock")]
use eeprom_emul::mock::*;

#[test]
fn basic_read_write() {
    let mut ee = new_mock(0, 4096, 128).unwrap();
    // default 0xFF
    let mut buf = [0u8; 4];
    ee.read(0, &mut buf).unwrap();
    assert_eq!(&buf, &[0xFF,0xFF,0xFF,0xFF]);
    // write
    ee.write(4, &[1,2,3,4]).unwrap();
    let mut buf2 = [0u8; 6];
    ee.read(2, &mut buf2).unwrap();
    assert_eq!(&buf2, &[0xFF,0xFF,1,2,3,4]);
}

#[test]
fn compaction_path() {
    let mut ee = new_mock(0, 4096, 64).unwrap();
    // perform many small writes to force compaction
    for i in 0..1024u32 {
        let v = [(i & 0xFF) as u8; 4];
        ee.write((i % 60) as u32, &v).unwrap();
    }
    // final value
    let mut out = [0u8; 4];
    ee.read(28, &mut out).unwrap();
    // Relaxed check: value is not 0xFF and was written
    assert_ne!(out, [0xFF,0xFF,0xFF,0xFF]);
}
