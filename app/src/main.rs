use anyhow::{anyhow, bail, Result};
use clap::{Args, Parser, Subcommand, ValueEnum};
use eeprom_emul::mock::new_mock;
use std::io::{self, Write};

const DEFAULT_SECTOR_SIZE: u32 = 4096;
const DEFAULT_EEPROM_SIZE: u32 = 1024; // 1 KiB logical
const DEFAULT_BASE: u32 = 0;

// Fixed layout for demo
const BOOT_COUNTER_ADDR: u32 = 0x0000; // u32 LE
// Simple KV region (fixed offsets)
const KV_NAME_ADDR: u32 = 0x0010; // 32 bytes
const KV_NAME_LEN: usize = 32;
const KV_BAUD_ADDR: u32 = 0x0030; // u32 LE
const KV_MODE_ADDR: u32 = 0x0034; // u8

#[derive(Parser, Debug)]
#[command(name = "eeprom_demo", version, about = "Mock EEPROM demo CLI", disable_help_subcommand = true)]
struct Cli {
    #[command(subcommand)]
    cmd: Command,

    /// Base address inside flash
    #[arg(long, default_value_t = DEFAULT_BASE)]
    base: u32,
    /// Sector size (bytes)
    #[arg(long, default_value_t = DEFAULT_SECTOR_SIZE)]
    sector_size: u32,
    /// Logical EEPROM size (bytes)
    #[arg(long, default_value_t = DEFAULT_EEPROM_SIZE)]
    size: u32,
}

#[derive(Subcommand, Debug)]
enum Command {
    /// Print demo geometry and layout
    Info,
    /// Reinitialize (format) the EEPROM
    Format,
    /// Read bytes at address
    Read { addr: u32, len: u32 },
    /// Write bytes at address
    Write(WriteArgs),
    /// Dump a region in hex
    Dump { offset: Option<u32>, len: Option<u32> },
    /// Boot counter (optionally increment)
    Boot { #[arg(long)] inc: bool },
    /// Simple fixed-offset key/value ops
    Kv { #[command(subcommand)] cmd: KvCmd },
    /// Interactive shell
    Repl,
}

#[derive(Args, Debug)]
struct WriteArgs {
    addr: u32,
    #[arg(long, conflicts_with = "str")] hex: Option<String>,
    #[arg(long, conflicts_with = "hex")] str: Option<String>,
}

#[derive(Subcommand, Debug)]
enum KvCmd {
    Get { key: KvKey },
    Set { key: KvKey, value: String },
}

#[derive(Copy, Clone, Debug, ValueEnum)]
enum KvKey { Name, Baud, Mode }

fn main() -> Result<()> {
    let cli = Cli::parse();
    run_cli(cli)
}

fn run_cli(cli: Cli) -> Result<()> {
    let mut ee = new_mock(cli.base, cli.sector_size, cli.size)?;
    match cli.cmd {
        Command::Info => {
            print_info(cli.base, cli.sector_size, cli.size)?;
        }
        Command::Format => {
            // Create a fresh instance to run init/format logic, then drop it
            let _ = new_mock(cli.base, cli.sector_size, cli.size)?;
            println!("Formatted EEPROM ({} bytes, sector {}).", cli.size, cli.sector_size);
        }
        Command::Read { addr, len } => {
            let mut buf = vec![0u8; len as usize];
            ee.read(addr, &mut buf)?;
            println!("{}", hex::encode(buf));
        }
        Command::Write(w) => {
            let data = if let Some(h) = w.hex { decode_hex(&h)? } else if let Some(s) = w.str { s.into_bytes() } else { bail!("Provide --hex or --str"); };
            ee.write(w.addr, &data)?;
            println!("Wrote {} bytes at 0x{:04X}", data.len(), w.addr);
        }
        Command::Dump { offset, len } => {
            let off = offset.unwrap_or(0);
            let ln = len.unwrap_or(cli.size - off);
            let mut buf = vec![0u8; ln as usize];
            ee.read(off, &mut buf)?;
            hexdump(off as usize, &buf);
        }
        Command::Boot { inc } => {
            let mut b = [0u8; 4];
            ee.read(BOOT_COUNTER_ADDR, &mut b)?;
            let mut cnt = u32::from_le_bytes(b);
            println!("boot_count={}", cnt);
            if inc {
                cnt = cnt.wrapping_add(1);
                ee.write(BOOT_COUNTER_ADDR, &cnt.to_le_bytes())?;
                println!("incremented -> {}", cnt);
            }
        }
        Command::Kv { cmd } => match cmd {
            KvCmd::Get { key } => kv_get(&mut ee, key)?,
            KvCmd::Set { key, value } => kv_set(&mut ee, key, &value)?,
        },
        Command::Repl => repl(cli.base, cli.sector_size, cli.size)?,
    }
    Ok(())
}

fn print_info(base: u32, sector_size: u32, size: u32) -> Result<()> {
    println!("Geometry:");
    println!("- base:        0x{:08X}", base);
    println!("- sector_size: {}", sector_size);
    println!("- eeprom_size: {}", size);
    println!("Layout:");
    println!("- boot_count @ 0x{:04X} (u32)", BOOT_COUNTER_ADDR);
    println!("- name       @ 0x{:04X} ({}B)", KV_NAME_ADDR, KV_NAME_LEN);
    println!("- baud       @ 0x{:04X} (u32)", KV_BAUD_ADDR);
    println!("- mode       @ 0x{:04X} (u8)", KV_MODE_ADDR);
    Ok(())
}

fn kv_get<F: flash_core::Flash>(ee: &mut eeprom_emul::Eeprom<F>, key: KvKey) -> Result<()> {
    match key {
        KvKey::Name => {
            let mut buf = vec![0u8; KV_NAME_LEN];
            ee.read(KV_NAME_ADDR, &mut buf)?;
            let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
            let s = String::from_utf8_lossy(&buf[..end]).to_string();
            println!("name={}", s);
        }
        KvKey::Baud => {
            let mut b = [0u8; 4];
            ee.read(KV_BAUD_ADDR, &mut b)?;
            println!("baud={}", u32::from_le_bytes(b));
        }
        KvKey::Mode => {
            let mut b = [0u8; 1];
            ee.read(KV_MODE_ADDR, &mut b)?;
            println!("mode={}", b[0]);
        }
    }
    Ok(())
}

fn kv_set<F: flash_core::Flash>(ee: &mut eeprom_emul::Eeprom<F>, key: KvKey, value: &str) -> Result<()> {
    match key {
        KvKey::Name => {
            let mut buf = [0u8; KV_NAME_LEN];
            let bytes = value.as_bytes();
            if bytes.len() > KV_NAME_LEN { bail!("name too long (max {} bytes)", KV_NAME_LEN); }
            buf[..bytes.len()].copy_from_slice(bytes);
            ee.write(KV_NAME_ADDR, &buf)?;
            println!("ok");
        }
        KvKey::Baud => {
            let v: u32 = value.parse().map_err(|_| anyhow!("invalid u32"))?;
            ee.write(KV_BAUD_ADDR, &v.to_le_bytes())?;
            println!("ok");
        }
        KvKey::Mode => {
            let v: u8 = value.parse().map_err(|_| anyhow!("invalid u8"))?;
            ee.write(KV_MODE_ADDR, &[v])?;
            println!("ok");
        }
    }
    Ok(())
}

fn repl(base: u32, sector: u32, size: u32) -> Result<()> {
    let mut ee = new_mock(base, sector, size)?;
    let mut rl = rustyline::Editor::<(), _>::new()?;
    println!("Mock EEPROM REPL. Type 'help' or 'quit'.");
    loop {
        let line = rl.readline("eeprom> ");
        let line = match line {
            Ok(s) => s,
            Err(_) => break,
        };
        let line = line.trim();
        if line.is_empty() { continue; }
        rl.add_history_entry(line).ok();
        match handle_repl_line(&mut ee, line, base, sector, size) {
            Ok(Control::Continue) => {}
            Ok(Control::Quit) => break,
            Err(e) => eprintln!("error: {}", e),
        }
        io::stdout().flush().ok();
    }
    Ok(())
}

enum Control { Continue, Quit }

fn handle_repl_line<F: flash_core::Flash>(ee: &mut eeprom_emul::Eeprom<F>, line: &str, base: u32, sector: u32, size: u32) -> Result<Control> {
    let parts = shellwords(line);
    if parts.is_empty() { return Ok(Control::Continue); }
    match parts[0] {
        "help" => {
            println!("commands: info, format, read <addr> <len>, write <addr> --hex <bytes>|--str <text>, dump [off] [len], boot [--inc], kv get <name|baud|mode>, kv set <...>, quit");
        }
        "quit" | "exit" => return Ok(Control::Quit),
        "info" => { print_info(base, sector, size)?; }
        "format" => {
            // Recreate ee by swapping in a fresh one (cannot reassign &mut param)
            // Workaround: erase by writing a full snapshot of 0xFF via compaction path.
            // Simpler: replace state logically by writing zeros to known KV region and boot counter.
            // For demo, emulate format by resetting boot counter and KV area.
            ee.write(BOOT_COUNTER_ADDR, &0u32.to_le_bytes())?;
            ee.write(KV_BAUD_ADDR, &115200u32.to_le_bytes())?;
            let name = [0u8; KV_NAME_LEN];
            ee.write(KV_NAME_ADDR, &name)?;
            ee.write(KV_MODE_ADDR, &[0])?;
            println!("formatted (demo reset)");
        }
        "read" => {
            if parts.len() < 3 { bail!("usage: read <addr> <len>"); }
            let addr = parse_u32(parts[1])?; let len = parse_u32(parts[2])?;
            let mut buf = vec![0u8; len as usize];
            ee.read(addr, &mut buf)?;
            println!("{}", hex::encode(buf));
        }
        "write" => {
            if parts.len() < 3 { bail!("usage: write <addr> (--hex <bytes> | --str <text>)"); }
            let addr = parse_u32(parts[1])?;
            let mut data: Option<Vec<u8>> = None;
            let mut i = 2;
            while i < parts.len() {
                match parts[i] {
                    "--hex" => { i+=1; if i>=parts.len() { bail!("missing hex"); } data = Some(decode_hex(parts[i])?); },
                    "--str" => { i+=1; if i>=parts.len() { bail!("missing str"); } data = Some(parts[i].as_bytes().to_vec()); },
                    _ => bail!("unknown flag {}", parts[i]),
                }
                i+=1;
            }
            let data = data.ok_or_else(|| anyhow!("provide --hex or --str"))?;
            ee.write(addr, &data)?;
            println!("ok");
        }
        "dump" => {
            let off = parts.get(1).map(|s| parse_u32(s)).transpose()?.unwrap_or(0);
            let len = parts.get(2).map(|s| parse_u32(s)).transpose()?.unwrap_or(DEFAULT_EEPROM_SIZE - off);
            let mut buf = vec![0u8; len as usize];
            ee.read(off, &mut buf)?;
            hexdump(off as usize, &buf);
        }
        "boot" => {
            let inc = parts.iter().any(|s| *s == "--inc");
            let mut b = [0u8; 4]; ee.read(BOOT_COUNTER_ADDR, &mut b)?; let mut cnt = u32::from_le_bytes(b);
            println!("boot_count={}", cnt);
            if inc { cnt = cnt.wrapping_add(1); ee.write(BOOT_COUNTER_ADDR, &cnt.to_le_bytes())?; println!("incremented -> {}", cnt); }
        }
        "kv" => {
            if parts.len() < 3 { bail!("usage: kv <get|set> ..."); }
            match parts[1] {
                "get" => {
                    let key = parse_key(parts.get(2).copied())?;
                    kv_get(ee, key)?;
                }
                "set" => {
                    if parts.len() < 4 { bail!("usage: kv set <key> <value>"); }
                    let key = parse_key(parts.get(2).copied())?;
                    kv_set(ee, key, parts[3])?;
                }
                c => bail!("unknown kv cmd {}", c),
            }
        }
        other => bail!("unknown command {}", other),
    }
    Ok(Control::Continue)
}

fn parse_key(name: Option<&str>) -> Result<KvKey> { match name {
    Some("name") => Ok(KvKey::Name),
    Some("baud") => Ok(KvKey::Baud),
    Some("mode") => Ok(KvKey::Mode),
    _ => bail!("unknown key (name|baud|mode)"),
} }

fn parse_u32(s: &str) -> Result<u32> {
    if let Some(rest) = s.strip_prefix("0x") { u32::from_str_radix(rest, 16).map_err(|_| anyhow!("invalid u32")) }
    else { s.parse::<u32>().map_err(|_| anyhow!("invalid u32")) }
}

fn decode_hex(s: &str) -> Result<Vec<u8>> {
    let s = s.trim();
    let s = s.strip_prefix("0x").unwrap_or(s);
    let s = s.replace(' ', "").replace('_', "");
    if s.len() % 2 != 0 { bail!("hex must have even length"); }
    hex::decode(s).map_err(|e| anyhow!("{}", e))
}

fn hexdump(start: usize, data: &[u8]) {
    let mut off = 0usize;
    while off < data.len() {
        let line = &data[off..data.len().min(off + 16)];
        print!("{:08X}: ", start + off);
        for i in 0..16 { if i < line.len() { print!("{:02X} ", line[i]); } else { print!("   "); } }
        print!(" | ");
        for &b in line { let c = if (0x20..=0x7E).contains(&b) { b as char } else { '.' }; print!("{}", c); }
        println!();
        off += 16;
    }
}

fn shellwords(s: &str) -> Vec<&str> {
    // Minimal split by whitespace; quoted strings are not handled for simplicity
    s.split_whitespace().collect()
}
