Software Layer (C)

Overview
- Hosts the C low‑level SPI NOR flash driver and a pure‑C simulation that mirrors the HDL model’s behavior via an AXI‑Lite‑style register interface.
- Used for fast iteration and unit testing without HDL; later bridged into Rust.

Layout
- `flash_ll/`: low‑level flash driver + register map
  - `include/flash_ll.h`: public API (driver context, IO vtable)
  - `include/flash_ll_regs.h`: command and register constants
  - `src/flash_ll.c`: driver implementation
  - `sim/`: pure‑C simulation harness, model, and tests
    - `include/flash_ll_io_sim.h`: IO bridge for driver→sim
    - `include/flash_sim.h`, `include/axi_spi_sim.h`: model + AXI register shim
    - `src/flash_sim.c`, `src/axi_spi_sim.c`: implementations
    - `src/sim_main.c`: model and driver tests

Build & Run (Simulation)
- Unix:
  - `cmake -S sw/flash_ll -B sw/flash_ll/build`
  - `cmake --build sw/flash_ll/build --config Release`
  - `./sw/flash_ll/build/sim/sim_flash_ll`
- Windows (Visual Studio):
  - Open “x64 Native Tools Command Prompt for VS”
  - `cmake -S sw/flash_ll -B sw/flash_ll/build -G "Visual Studio 17 2022"`
  - `cmake --build sw/flash_ll/build --config Release`
  - `sw\flash_ll\build\sim\Release\sim_flash_ll.exe`

What’s Implemented
- Model: NOR flash with WEL/WIP semantics, page‑bounded program, sector erase, 1→0 rule (AND semantics).
- AXI‑Lite shim: register map with `CMD/ADDR/LEN/DIN/DOUT/CTRL/STATUS` and DIN/DOUT FIFOs.
- Driver: `flash_ll_*` issues WREN/RDSR/READ/PP/SE, handles page chunking, polls WIP.
- Tests: model‑level and driver‑level scenarios mirroring the HDL TB.

Test Locations
- Model tests: `sw/flash_ll/sim/src/sim_main.c` (`test_...` cases)
- Driver tests: same file (`drv_...` cases)
- Harness macros: `sw/flash_ll/sim/include/sim_test.h`

Rust Mapping (Plan)
- Create a Rust `flash_ll_sys` crate that builds the C driver and sim with the `cc` crate and generates FFI with `bindgen` from `flash_ll.h` and `flash_ll_regs.h`.
- Add a safe Rust wrapper crate `flash_ll` that:
  - Wraps `FlashLlCtx` into a RAII type with lifetimes for the IO backend.
  - Exposes idiomatic methods: `read`, `program`, `sector_erase`, `rdsr`, `wait_busy`.
  - Optionally re‑exports a “sim backend” using the C sim to run unit tests in Rust.
- Testing in Rust:
  - Mirror the C scenarios using the Rust wrapper with the sim backend to ensure API parity.
  - Optionally include property tests (e.g., page boundary and AND semantics).

Rust Crates
- `sw/rust/flash_core`: pure‑Rust `Flash` trait and geometry type
- `sw/rust/flash_mock`: pure‑Rust in‑memory NOR implementing `Flash`
- `sw/rust/flash_ll_sys`: FFI crate (bindgen + cc) for C driver/sim
- `sw/rust/flash_ll`: safe wrapper over C driver (sim helper included)
- `sw/rust/eeprom_emul`: EEPROM emulation generic over `Flash`
  - Default uses `flash_mock` (no C/LLVM). Optional `ffi` feature can use C driver instead.

Build Rust (workspace)
- From the repo root:
  - `cargo test -p flash_core`
  - `cargo test -p flash_mock`
  - `cargo test -p flash_ll_sys`
  - `cargo test -p flash_ll`
  - `cargo test -p eeprom_emul`

Cargo Aliases
- `cargo eeprom-mock`: run EEPROM tests with the pure‑Rust mock backend (no C/LLVM).
- `cargo eeprom-ffi`: run EEPROM tests via the C‑backed driver (requires LLVM/bindgen).
- `cargo ffi-all`: build/test the C FFI crates only.

Notes
- C/LLVM only required if building the C driver/simulator.
  - Windows: install LLVM (clang) and set `LIBCLANG_PATH` (e.g., `C:\\Program Files\\LLVM\\bin`).
- Use the pure‑Rust path (no C/LLVM) for virtual EEPROM:
  - `cargo test -p eeprom_emul --no-default-features --features mock`
- Enable C driver/simulator only when needed:
  - `cargo test -p eeprom_emul --no-default-features --features ffi`
 - Default workspace members target pure‑Rust crates for faster default builds; add `-p` or use aliases for others.
