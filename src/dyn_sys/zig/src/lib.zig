const std = @import("std");
const mem = std.mem;
const Allocator = std.mem.Allocator;

pub const type_def_obj = extern struct {};
pub const typed_obj = extern struct {
    ptr: ?*anyopaque,
    type: ?*const type_def_obj,
};

pub const dyn_param_desc = extern struct {
    name: ?[*:0]const u8,
    type_name: ?[*:0]const u8,
    desc: ?[*:0]const u8,
};

pub const dyn_method_sig = extern struct {
    ret_type_name: ?[*:0]const u8,
    params: ?[*]const dyn_param_desc,
    param_count: c_int,
    desc: ?[*:0]const u8,
};

pub const dyn_method_call_fn = ?*const fn (self: *typed_obj, args: ?*const typed_obj, argc: c_int, ret_out: *typed_obj) callconv(.C) c_int;

const OpEntry = struct {
    name: [*:0]const u8,
    sig_str: ?[*:0]const u8,
    desc: ?[*:0]const u8,
    typed_sig: ?*dyn_method_sig,
    impl_fn: dyn_method_call_fn,
};

const TypeEntry = struct {
    name: [*:0]const u8,
    ops: std.ArrayList(OpEntry),
};

var g_allocator: Allocator = undefined;
var g_types: std.ArrayList(TypeEntry) = undefined;
var g_inited: bool = false;

fn ensureInit() void {
    if (!g_inited) {
        g_allocator = std.heap.page_allocator;
        g_types = std.ArrayList(TypeEntry).init(g_allocator);
        g_inited = true;
    }
}

fn to_cstr(s: []const u8) ![*:0]u8 {
    var buf = try g_allocator.alloc(u8, s.len + 1);
    @memcpy(buf[0..s.len], s);
    buf[s.len] = 0;
    return @ptrCast(buf);
}

fn cspan(p: [*:0]const u8) []const u8 {
    return mem.span(p);
}

fn findTypeIndex(type_name: [*:0]const u8) ?usize {
    const name = cspan(type_name);
    for (g_types.items, 0..) |te, i| {
        if (mem.eql(u8, cspan(te.name), name)) return i;
    }
    return null;
}

fn build_sig_string(sig: *const dyn_method_sig) ![*:0]u8 {
    var buf = std.ArrayList(u8).init(g_allocator);
    defer buf.deinit();
    const ret = if (sig.ret_type_name) |r| cspan(r) else "void";
    try buf.appendSlice(ret);
    try buf.append('(');
    const pc = @as(usize, @intCast(sig.param_count));
    if (pc > 0 and sig.params != null) {
        const params = sig.params.?;
        var i: usize = 0;
        while (i < pc) : (i += 1) {
            const tn = if (params[i].type_name) |t| cspan(t) else "void";
            try buf.appendSlice(tn);
            if (i + 1 < pc) try buf.appendSlice(", ");
        }
    }
    try buf.append(')');
    return try to_cstr(buf.items);
}

fn ensureType(type_name: [*:0]const u8) !usize {
    ensureInit();
    if (findTypeIndex(type_name)) |idx| return idx;
    const name_dup = try to_cstr(cspan(type_name));
    const ops = std.ArrayList(OpEntry).init(g_allocator);
    try g_types.append(.{ .name = name_dup, .ops = ops });
    return g_types.items.len - 1;
}

fn findOpIndex(idx: usize, op_name: [*:0]const u8) ?usize {
    const name = cspan(op_name);
    const ops = g_types.items[idx].ops.items;
    for (ops, 0..) |op, i| {
        if (mem.eql(u8, cspan(op.name), name)) return i;
    }
    return null;
}

pub export fn dyn_oop_register_type(type_name: ?[*:0]const u8) c_int {
    if (type_name == null) return -1;
    ensureInit();
    const tn = type_name.?;
    if (findTypeIndex(tn) != null) return 0;
    _ = ensureType(tn) catch return -1;
    return 0;
}

pub export fn dyn_oop_get_type_count() c_int {
    ensureInit();
    return @as(c_int, @intCast(g_types.items.len));
}

pub export fn dyn_oop_get_type_name(index: c_int) ?[*:0]const u8 {
    ensureInit();
    const i: usize = @intCast(index);
    if (i >= g_types.items.len) return null;
    return g_types.items[i].name;
}

pub export fn dyn_oop_get_op_count(type_name: [*:0]const u8) c_int {
    ensureInit();
    const idx = findTypeIndex(type_name) orelse return -1;
    return @as(c_int, @intCast(g_types.items[idx].ops.items.len));
}

pub export fn dyn_oop_get_op_name(type_name: [*:0]const u8, index: c_int) ?[*:0]const u8 {
    ensureInit();
    const idx = findTypeIndex(type_name) orelse return null;
    const i: usize = @intCast(index);
    const ops = g_types.items[idx].ops.items;
    if (i >= ops.len) return null;
    return ops[i].name;
}

pub export fn dyn_oop_get_op_meta(
    type_name: [*:0]const u8,
    index: c_int,
    name_out: ?*?[*:0]const u8,
    sig_out: ?*?[*:0]const u8,
    desc_out: ?*?[*:0]const u8,
) c_int {
    ensureInit();
    const idx = findTypeIndex(type_name) orelse return -1;
    const i: usize = @intCast(index);
    const ops = g_types.items[idx].ops.items;
    if (i >= ops.len) return -1;
    const op = ops[i];
    if (name_out) |p| p.* = op.name;
    if (sig_out) |p| p.* = op.sig_str;
    if (desc_out) |p| p.* = op.desc;
    return 0;
}

pub export fn dyn_oop_register_op_typed(
    type_name: ?[*:0]const u8,
    op_name: ?[*:0]const u8,
    sig: ?*const dyn_method_sig,
    op_desc: ?[*:0]const u8,
    fn_ptr: dyn_method_call_fn,
) c_int {
    if (type_name == null or op_name == null or sig == null or fn_ptr == null) return -1;
    ensureInit();
    const tn = type_name.?;
    const on = op_name.?;
    const idx = ensureType(tn) catch return -1;
    const ops_list = &g_types.items[idx].ops;
    const sig_ptr = sig.?;

    const op_idx = findOpIndex(idx, on) orelse blk: {
        const name_dup = to_cstr(cspan(on)) catch return -1;
        const sig_str = build_sig_string(sig_ptr) catch null;
        const desc_dup = if (op_desc) |d| to_cstr(cspan(d)) catch null else null;
        const sig_copy = g_allocator.create(dyn_method_sig) catch return -1;
        sig_copy.* = sig_ptr.*;
        const entry = OpEntry{
            .name = name_dup,
            .sig_str = sig_str,
            .desc = desc_dup,
            .typed_sig = sig_copy,
            .impl_fn = fn_ptr,
        };
        ops_list.append(entry) catch return -1;
        break :blk ops_list.items.len - 1;
    };

    // Update existing op
    var op = &ops_list.items[op_idx];
    op.impl_fn = fn_ptr;
    op.desc = if (op_desc) |d| to_cstr(cspan(d)) catch null else null;
    op.sig_str = build_sig_string(sig_ptr) catch op.sig_str;
    if (op.typed_sig) |p| p.* = sig_ptr.* else {
        const sig_copy = g_allocator.create(dyn_method_sig) catch return -1;
        sig_copy.* = sig_ptr.*;
        op.typed_sig = sig_copy;
    }
    return 0;
}

pub export fn dyn_oop_get_op_typed(
    type_name: [*:0]const u8,
    op_name: [*:0]const u8,
    sig_out: ?*?*const dyn_method_sig,
) c_int {
    ensureInit();
    const idx = findTypeIndex(type_name) orelse return -1;
    const oi = findOpIndex(idx, op_name) orelse return -1;
    const op = g_types.items[idx].ops.items[oi];
    if (op.typed_sig == null) return -1;
    if (sig_out) |p| p.* = op.typed_sig.?;
    return 0;
}

pub export fn dyn_oop_do_op(
    type_name: [*:0]const u8,
    op_name: [*:0]const u8,
    self: *typed_obj,
    args: ?*const typed_obj,
    argc: c_int,
    out: *typed_obj,
) c_int {
    ensureInit();
    const idx = findTypeIndex(type_name) orelse return -1;
    const oi = findOpIndex(idx, op_name) orelse return -1;
    const op = g_types.items[idx].ops.items[oi];
    if (op.impl_fn == null) return -1;
    // 简单校验：参数个数匹配
    if (op.typed_sig) |ts| {
        if (argc != ts.param_count) {
            // 允许不严格校验：只在>0时校验
            if (ts.param_count > 0) return -1;
        }
    }
    return op.impl_fn.?(self, args, argc, out);
}

test "register type and op, query meta, invoke" {
    const T = "Test";
    const add = "add";
    // Build signature: i64(i64, i64)
    var params = [_]dyn_param_desc{
        .{ .name = null, .type_name = "i64", .desc = null },
        .{ .name = null, .type_name = "i64", .desc = null },
    };
    var sig = dyn_method_sig{
        .ret_type_name = "i64",
        .params = &params,
        .param_count = 2,
        .desc = null,
    };
    const rc_t = dyn_oop_register_type(T);
    try std.testing.expect(rc_t == 0);
    const rc_op = dyn_oop_register_op_typed(T, add, &sig, null, add_cb);
    try std.testing.expect(rc_op == 0);
    try std.testing.expect(dyn_oop_get_type_count() == 1);
    try std.testing.expect(dyn_oop_get_op_count(T) == 1);
    const oname = dyn_oop_get_op_name(T, 0).?;
    try std.testing.expect(mem.eql(u8, cspan(oname), add));
    var n: ?[*:0]const u8 = null;
    var s: ?[*:0]const u8 = null;
    var d: ?[*:0]const u8 = null;
    const mrc = dyn_oop_get_op_meta(T, 0, &n, &s, &d);
    try std.testing.expect(mrc == 0);
    try std.testing.expect(n != null and s != null);
    const sigstr = cspan(s.?);
    try std.testing.expect(mem.eql(u8, sigstr, "i64(i64, i64)"));
    var self: typed_obj = .{ .ptr = null, .type = null };
    var out: typed_obj = .{ .ptr = null, .type = null };
    const call_rc = dyn_oop_do_op(T, add, &self, null, 2, &out);
    try std.testing.expect(call_rc == 42);
}

pub export fn add_cb(self: *typed_obj, args: ?*const typed_obj, argc: c_int, out: *typed_obj) c_int {
    _ = self;
    _ = args;
    _ = argc;
    _ = out;
    return 42;
}
