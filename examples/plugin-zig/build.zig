const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "plugin_zig_demo",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });
    exe.addIncludePath(b.path("../../src/include"));
    exe.addObjectFile(b.path("../../build/lib/Release/tinybuf.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/dyn_integration.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/dyn_sys.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/jsoncpp.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/dyncall_s.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/dyncallback_s.lib"));
    exe.addObjectFile(b.path("../../build/lib/Release/dynload_s.lib"));
    // rely on linkLibC to resolve system libs
    b.installArtifact(exe);

    const run_step = b.step("run", "Run plugin zig demo");
    const cmd = b.addRunArtifact(exe);
    run_step.dependOn(&cmd.step);

    const lib = b.addSharedLibrary(.{
        .name = "upper_plugin_zig",
        .root_source_file = b.path("src/plugin.zig"),
        .target = target,
        .optimize = optimize,
    });
    lib.addIncludePath(b.path("../../src/include"));
    lib.addObjectFile(b.path("../../build/lib/Release/tinybuf.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/dyn_integration.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/dyn_sys.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/jsoncpp.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/dyncall_s.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/dyncallback_s.lib"));
    lib.addObjectFile(b.path("../../build/lib/Release/dynload_s.lib"));
    b.installArtifact(lib);
    const lib_step = b.step("build-lib", "Build zig plugin dll");
    lib_step.dependOn(&lib.step);

    if (@import("builtin").os.tag == .windows) {
        exe.linkSystemLibrary("ucrt");
        exe.linkSystemLibrary("vcruntime");
        exe.linkSystemLibrary("ws2_32");
        lib.linkSystemLibrary("ucrt");
        lib.linkSystemLibrary("vcruntime");
        lib.linkSystemLibrary("ws2_32");
    }
}
