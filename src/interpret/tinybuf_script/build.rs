use std::env;
use std::path::PathBuf;

fn main() {
    lalrpop::process_root().expect("lalrpop generate failed");
    if env::var("CARGO_FEATURE_ZIG_OOP").is_err() {
        return;
    }
    let crate_dir = std::fs::canonicalize(env::var("CARGO_MANIFEST_DIR").unwrap()).unwrap();
    let zig_out_dir = crate_dir
        .parent().unwrap() // src/interpret
        .parent().unwrap() // src
        .join("dyn_sys").join("zig").join("zig-out");
    let zig_lib_dir = zig_out_dir.join("lib");
    let zig_bin_dir = zig_out_dir.join("bin");
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    if target_os == "windows" {
        let import_lib = zig_lib_dir.join("dyn_sys_zig_dll.lib");
        if import_lib.exists() {
            println!("cargo:rustc-link-search=native={}", zig_lib_dir.display());
            println!("cargo:rustc-link-lib=dylib={}", "dyn_sys_zig_dll");
            let dll_src = zig_bin_dir.join("dyn_sys_zig_dll.dll");
            if dll_src.exists() {
                let target_dir = crate_dir.join("target").join("debug");
                let deps_dir = target_dir.join("deps");
                let _ = std::fs::create_dir_all(&target_dir);
                let _ = std::fs::create_dir_all(&deps_dir);
                let dll_dst1 = target_dir.join("dyn_sys_zig_dll.dll");
                let dll_dst2 = deps_dir.join("dyn_sys_zig_dll.dll");
                let _ = std::fs::copy(&dll_src, &dll_dst1);
                let _ = std::fs::copy(&dll_src, &dll_dst2);
            }
            return;
        }
    }

    let (lib_file, link_name) = if target_os == "windows" {
        (zig_lib_dir.join("dyn_sys_zig.lib"), "dyn_sys_zig")
    } else {
        (zig_lib_dir.join("libdyn_sys_zig.a"), "dyn_sys_zig")
    };

    if !lib_file.exists() {
        panic!("zig dyn_sys library not found at: {}", lib_file.display());
    }

    println!("cargo:rustc-link-search=native={}", zig_lib_dir.display());
    println!("cargo:rustc-link-lib=static={}", link_name);
    if target_os == "windows" {
        println!("cargo:rustc-link-lib=vcruntime");
        println!("cargo:rustc-link-lib=libvcruntime");
        println!("cargo:rustc-link-lib=msvcrt");
        println!("cargo:rustc-link-lib=ucrt");
    }
}
