use std::{env, path::PathBuf};

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.parent().and_then(|p| p.parent()).unwrap().to_path_buf();
    let c_root = repo_root.join("flash_ll");
    let c_inc = c_root.join("include");
    let c_src = c_root.join("src");
    let sim_inc = c_root.join("sim").join("include");
    let sim_src = c_root.join("sim").join("src");

    // Build C sources
    let mut build = cc::Build::new();
    build.include(&c_inc).file(c_src.join("flash_ll.c"));
    let feat_sim = env::var("CARGO_FEATURE_SIM").is_ok();
    if feat_sim {
        build.include(&sim_inc)
            .file(sim_src.join("flash_sim.c"))
            .file(sim_src.join("axi_spi_sim.c"))
            .file(sim_src.join("flash_ll_io_sim.c"));
    }
    #[cfg(target_os = "windows")]
    build.flag_if_supported("/std:c11");
    #[cfg(not(target_os = "windows"))]
    build.flag_if_supported("-std=c11");
    build.compile("flash_ll_c");

    // Generate bindings
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let mut builder = bindgen::Builder::default()
        .clang_arg(format!("-I{}", c_inc.display()))
        .header(c_inc.join("flash_ll.h").to_string_lossy())
        .header(c_inc.join("flash_ll_regs.h").to_string_lossy())
        .allowlist_type("FlashLl.*|AxiSpiSim|FlashSim")
        .allowlist_function("flash_ll_.*|axi_spi_.*|flash_sim_.*|flash_ll_axi_sim_ops")
        .allowlist_var("FLASH_LL_.*|REG_.*|SPI_.*|FLASH_SIM_.*");

    if feat_sim {
        builder = builder
            .clang_arg(format!("-I{}", sim_inc.display()))
            .header(sim_inc.join("axi_spi_sim.h").to_string_lossy())
            .header(sim_inc.join("flash_sim.h").to_string_lossy())
            .header(sim_inc.join("flash_ll_io_sim.h").to_string_lossy());
    }

    let bindings = builder
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("bindgen failed");
    bindings
        .write_to_file(out.join("bindings.rs"))
        .expect("write bindings");
}
