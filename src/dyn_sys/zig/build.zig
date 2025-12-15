const std = @import("std");

pub fn build(b: *std.Build) void {
    const target =
        if (@hasDecl(std.Build, "standardTargetOptions"))
            b.standardTargetOptions(.{})
        else
            b.standardTargetOptions(.{});
    const optimize =
        if (@hasDecl(std.Build, "standardOptimizeOptions"))
            b.standardOptimizeOptions(.{})
        else
            b.standardOptimizeOption(.{});

    if (@hasDecl(std.Build, "addLibrary")) {
        const mod = b.createModule(.{
            .root_source_file = b.path("src/lib.zig"),
            .target = target,
            .optimize = optimize,
        });
        const lib = b.addLibrary(.{
            .name = "dyn_sys_zig",
            .root_module = mod,
            .linkage = .static,
        });
        b.installArtifact(lib);

        const dll = b.addLibrary(.{
            .name = "dyn_sys_zig_dll",
            .root_module = mod,
            .linkage = .dynamic,
        });
        b.installArtifact(dll);
    } else {
        const lib = b.addStaticLibrary(.{
            .name = "dyn_sys_zig",
            .root_source_file = b.path("src/lib.zig"),
            .target = target,
            .optimize = optimize,
        });
        b.installArtifact(lib);
        const dll = b.addSharedLibrary(.{
            .name = "dyn_sys_zig_dll",
            .root_source_file = b.path("src/lib.zig"),
            .target = target,
            .optimize = optimize,
        });
        b.installArtifact(dll);
    }

    const unit_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/lib.zig"),
            .target = target,
            .optimize = optimize,
        }),
        .link_libc = false,
    });
    const run_unit_tests = b.addRunArtifact(unit_tests);
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}
