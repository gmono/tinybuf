use std::env;
use std::path::PathBuf;

fn main() {
    let _ = lalrpop::process_root();
    let crate_dir = std::fs::canonicalize(env::var("CARGO_MANIFEST_DIR").unwrap()).unwrap();
    let zig_out_dir = crate_dir
        .parent().unwrap() // src/interpret
        .parent().unwrap() // src
        .join("dyn_sys").join("zig").join("zig-out");
    let zig_lib_dir = zig_out_dir.join("lib");
    let zig_bin_dir = zig_out_dir.join("bin");
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();

    if target_os == "windows" {
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
            return;
        }
    }

    let _ = ();
}
