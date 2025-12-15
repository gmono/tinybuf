const c = @cImport({
    @cInclude("tinybuf_plugin.h");
    @cInclude("tinybuf.h");
    @cInclude("tinybuf_buffer.h");
    @cInclude("tinybuf_memory.h");
});

const DLL_UPPER_TYPE: u8 = 201;

fn dll_upper_read(type_tag: u8, buf: [*c]c.buf_ref, out: ?*c.tinybuf_value, contain: ?*const fn (u64) callconv(.C) c_int, r: [*c]c.tinybuf_error) callconv(.C) c_int {
    _ = contain;
    _ = r;
    if (type_tag != DLL_UPPER_TYPE) return -1;
    const b: *c.buf_ref = @ptrCast(buf);
    if (b.size < 2) return 0;
    const tag: u8 = @intCast(b.ptr[0]);
    if (tag != DLL_UPPER_TYPE) return -1;
    const len: c_int = @intCast(b.ptr[1]);
    const need: @TypeOf(b.size) = @intCast(2 + len);
    if (b.size < need) return 0;
    const p = b.ptr + 2;
    const tmp: [*]u8 = @ptrCast(c.tinybuf_malloc(len));
    const ulen: usize = @intCast(len);
    var i: usize = 0;
    while (i < ulen) : (i += 1) {
        var ch = p[i];
        if (ch >= 'a' and ch <= 'z') ch = ch - 'a' + 'A';
        tmp[i] = ch;
    }
    _ = c.tinybuf_value_init_string(out.?, tmp, len);
    _ = c.tinybuf_value_set_plugin_index(out.?, c.tinybuf_plugin_get_runtime_index_by_tag(DLL_UPPER_TYPE));
    c.tinybuf_free(tmp);
    b.ptr = p + @as(usize, @intCast(len));
    b.size -= 2 + len;
    return 2 + len;
}

fn dll_upper_write(type_tag: u8, input: ?*const c.tinybuf_value, out: ?*c.buffer, r: [*c]c.tinybuf_error) callconv(.C) c_int {
    _ = r;
    if (type_tag != DLL_UPPER_TYPE) return -1;
    var er = c.tinybuf_result_ok(0);
    const s = c.tinybuf_value_get_string(input.?, &er);
    if (s == null) return -1;
    var len: c_int = c.buffer_get_length(s);
    if (len > 255) len = 255;
    const tag: u8 = DLL_UPPER_TYPE;
    _ = c.buffer_append(out.?, @ptrCast(&tag), 1);
    const l: u8 = @intCast(len);
    _ = c.buffer_append(out.?, @ptrCast(&l), 1);
    _ = c.buffer_append(out.?, c.buffer_get_data(s), len);
    return 2 + len;
}

fn dll_upper_dump(type_tag: u8, buf: [*c]c.buf_ref, out: ?*c.buffer, r: [*c]c.tinybuf_error) callconv(.C) c_int {
    _ = r;
    if (type_tag != DLL_UPPER_TYPE) return -1;
    const b: *c.buf_ref = @ptrCast(buf);
    if (b.size < 2) return 0;
    const tag: u8 = @intCast(b.ptr[0]);
    if (tag != DLL_UPPER_TYPE) return -1;
    const len: c_int = @intCast(b.ptr[1]);
    const need: @TypeOf(b.size) = @intCast(2 + len);
    if (b.size < need) return 0;
    const p = b.ptr + 2;
    _ = c.buffer_append(out.?, "\"", 1);
    const ulen: usize = @intCast(len);
    var i: usize = 0;
    while (i < ulen) : (i += 1) {
        var ch = p[i];
        if (ch >= 'a' and ch <= 'z') ch = ch - 'a' + 'A';
        _ = c.buffer_append(out.?, &ch, 1);
    }
    _ = c.buffer_append(out.?, "\"", 1);
    return 2 + len;
}

fn dll_upper_show_value(type_tag: u8, input: ?*const c.tinybuf_value, out: ?*c.buffer, r: [*c]c.tinybuf_error) callconv(.C) c_int {
    _ = r;
    if (type_tag != DLL_UPPER_TYPE) return -1;
    var er = c.tinybuf_result_ok(0);
    const s = c.tinybuf_value_get_string(input.?, &er);
    if (s == null) return -1;
    _ = c.buffer_append(out.?, "dll_upper(", 10);
    _ = c.buffer_append(out.?, c.buffer_get_data(s), c.buffer_get_length(s));
    _ = c.buffer_append(out.?, ")", 1);
    return c.buffer_get_length(s) + 11;
}

fn dll_to_lower(value: ?*c.tinybuf_value, args: ?*const c.tinybuf_value, out: ?*c.tinybuf_value) callconv(.C) c_int {
    _ = args;
    var er = c.tinybuf_result_ok(0);
    const s = c.tinybuf_value_get_string(value.?, &er);
    if (s == null) return -1;
    const len: c_int = c.buffer_get_length(s);
    const p = c.buffer_get_data(s);
    const tmp: [*]u8 = @ptrCast(c.tinybuf_malloc(len));
    const ulen: usize = @intCast(len);
    var i: usize = 0;
    while (i < ulen) : (i += 1) {
        const ch = p[i];
        tmp[i] = if (ch >= 'A' and ch <= 'Z') ch + ('a' - 'A') else ch;
    }
    _ = c.tinybuf_value_init_string(out.?, tmp, len);
    c.tinybuf_free(tmp);
    return 0;
}

var tags: [1]u8 = .{DLL_UPPER_TYPE};
const to_lower: [:0]const u8 = "to_lower";
const sig_str: [:0]const u8 = "string->string";
const desc_str: [:0]const u8 = "lowercase";
var names: [1][*c]const u8 = .{@ptrCast(to_lower.ptr)};
var sigs: [1][*c]const u8 = .{@ptrCast(sig_str.ptr)};
var descs: [1][*c]const u8 = .{@ptrCast(desc_str.ptr)};
var fns: [1]c.tinybuf_plugin_value_op_fn = .{dll_to_lower};
var desc: c.tinybuf_plugin_descriptor = .{
    .tags = &tags,
    .tag_count = 1,
    .guid = "dll:upper-string",
    .read = dll_upper_read,
    .write = dll_upper_write,
    .dump = dll_upper_dump,
    .show_value = dll_upper_show_value,
    .op_names = &names,
    .op_sigs = &sigs,
    .op_descs = &descs,
    .op_fns = &fns,
    .op_count = 1,
};

pub export fn tinybuf_get_plugin_descriptor() *c.tinybuf_plugin_descriptor {
    return &desc;
}
