use std::env;

fn main() {
    let manifest_dir = std::path::PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest_dir.parent().unwrap().parent().unwrap().parent().unwrap();

    let lib_dir = repo_root.join("build").join("lib").join("Release");
    println!("cargo:rustc-link-search=native={}", lib_dir.display());
    println!("cargo:rustc-link-lib=static=tinybuf");
    // link core integration and dyn_sys
    println!("cargo:rustc-link-lib=static=dyn_integration");
    println!("cargo:rustc-link-lib=dyncall_s");
    println!("cargo:rustc-link-lib=dyncallback_s");
    println!("cargo:rustc-link-lib=dynload_s");
    println!("cargo:rerun-if-changed={}", repo_root.join("src").display());

    // Zig dyn_sys static library
    let zig_lib_dir = repo_root.join("src").join("dyn_sys").join("zig").join("zig-out").join("lib");
    println!("cargo:rustc-link-search=native={}", zig_lib_dir.display());
    println!("cargo:rustc-link-lib=static=dyn_sys_zig");
}
