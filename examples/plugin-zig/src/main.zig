const std = @import("std");

const c = @cImport({
    @cInclude("tinybuf_plugin.h");
    @cInclude("tinybuf.h");
    @cInclude("tinybuf_buffer.h");
    @cInclude("tinybuf_memory.h");
});

const DLL_UPPER_TYPE: u8 = 201;

fn ParserFn(comptime T: type) type {
    return fn ([]const u8) T;
}

fn makeParser(comptime T: type, comptime endian: std.builtin.Endian) ParserFn(T) {
    const S = struct {
        pub fn parse(b: []const u8) T {
            switch (@typeInfo(T)) {
                .int => return std.mem.readInt(T, b[0..@sizeOf(T)], endian),
                else => @compileError("unsupported"),
            }
        }
    };
    return S.parse;
}

fn BinaryOps(comptime T: type, comptime endian: std.builtin.Endian) type {
    return struct {
        pub fn toBytes(v: T, out: []u8) void {
            switch (@typeInfo(T)) {
                .int => std.mem.writeInt(T, out[0..@sizeOf(T)], v, endian),
                else => @compileError("unsupported"),
            }
        }
        pub fn fromBytes(b: []const u8) T {
            switch (@typeInfo(T)) {
                .int => return std.mem.readInt(T, b[0..@sizeOf(T)], endian),
                else => @compileError("unsupported"),
            }
        }
    };
}

pub fn main() !void {
    _ = c.tinybuf_init();
    const dll = if (@import("builtin").os.tag == .windows)
        "../../tinybuf_plugins/system_extend.dll"
    else
        "../../tinybuf_plugins/libsystem_extend.so";
    const r = c.tinybuf_plugin_register_from_dll(dll);
    std.debug.print("register ret={d}\n", .{r});
    const cnt = c.tinybuf_plugin_get_count();
    std.debug.print("plugin count={d}\n", .{cnt});
    var i: c_int = 0;
    while (i < cnt) : (i += 1) {
        const g = c.tinybuf_plugin_get_guid(i);
        if (g != null) {
            const s = std.mem.span(g);
            std.debug.print("plugin[{d}]={s}\n", .{ i, s });
        }
    }
    const parse_u32 = makeParser(u32, .little);
    const v32 = parse_u32(&[_]u8{ 0x78, 0x56, 0x34, 0x12 });
    std.debug.print("u32_le={d}\n", .{v32});
    const U16BE = BinaryOps(u16, .big);
    var buf16: [2]u8 = undefined;
    U16BE.toBytes(0x1234, buf16[0..]);
    const back16 = U16BE.fromBytes(buf16[0..]);
    std.debug.print("u16_be=0x{X}\n", .{back16});
    const mydll = if (@import("builtin").os.tag == .windows)
        "zig-out/bin/upper_plugin_zig.dll"
    else
        "zig-out/lib/libupper_plugin_zig.so";
    const r2 = c.tinybuf_plugin_register_from_dll(mydll);
    std.debug.print("register zig dll ret={d}\n", .{r2});
    const lower: [*:0]const u8 = "to_lower";
    const v = c.tinybuf_value_alloc();
    const out = c.tinybuf_value_alloc();
    _ = c.tinybuf_value_init_string(v, "ABC", 3);
    const rcop = c.tinybuf_plugin_do_value_op_by_tag(DLL_UPPER_TYPE, lower, v, null, out);
    std.debug.print("op rc={d}\n", .{rcop});
    var er = c.tinybuf_result_ok(0);
    const buf = c.tinybuf_value_get_string(out, &er);
    if (buf != null) {
        const data = c.buffer_get_data(buf);
        const len = c.buffer_get_length(buf);
        const slice = data[0..@intCast(len)];
        std.debug.print("to_lower -> {s}\n", .{slice});
    }
}
