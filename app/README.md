EEPROM Demo App

Overview
- Mock-backed EEPROM shell and CLI for experimenting with the `eeprom_emul` crate.
- No hardware or C/LLVM required; uses the pure‑Rust mock flash.

Run
- Cargo alias (from repo root): `cargo eeprom-demo -- <cmd>`
- Direct: `cargo run --manifest-path app/Cargo.toml -- <cmd>`

Geometry Defaults
- `--base 0x0` `--sector-size 4096` `--size 1024`
- Two-sector log (8 KiB total mock flash) with 1 KiB logical EEPROM.

Commands
- `info`: Print geometry and layout.
- `format`: Reset demo values (boot counter, KV area).
- `read <addr> <len>`: Read logical EEPROM as hex.
- `write <addr> (--hex <bytes> | --str <text>)`: Write at address.
- `dump [offset] [len]`: Hex dump of a region.
- `boot [--inc]`: Show and optionally increment boot counter at `0x0000`.
- `kv get <name|baud|mode>` / `kv set <...>`: Fixed-offset settings.
- `repl`: Interactive shell providing the same commands.

Layout Map
- `0x0000..0x0004`: Boot counter (u32 LE)
- `0x0010..0x0030`: Name (32 bytes, NUL‑padded)
- `0x0030..0x0034`: Baud (u32 LE)
- `0x0034`: Mode (u8)

Examples
- Show help: `cargo eeprom-demo -- --help`
- Inspect: `cargo eeprom-demo -- info`
- Boot counter: `cargo eeprom-demo -- boot --inc`
- KV set/get:
  - `cargo eeprom-demo -- kv set name "Board A"`
  - `cargo eeprom-demo -- kv get name`
- Raw write/read:
  - `cargo eeprom-demo -- write 0x40 --str "hello"`
  - `cargo eeprom-demo -- read 0x40 5`
- Dump:
  - `cargo eeprom-demo -- dump 0 64`
- REPL: `cargo eeprom-demo -- repl`

Notes
- The REPL `format` performs a demo reset (writes default values). The CLI `format` constructs a fresh instance (equivalent to power‑on format).
- To experiment with different sizes: add `--sector-size` and `--size` flags to any command.

