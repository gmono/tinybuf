const std = @import("std");
const mem = std.mem;
const Allocator = std.mem.Allocator;

pub const CStr = ?[*:0]const u8;

pub const init_fn = *const fn (self: ?*anyopaque) callconv(.C) void;
pub const deleter_fn = *const fn (self: ?*anyopaque) callconv(.C) void;
pub const copy_fn = *const fn (from: ?*const anyopaque, to: ?*anyopaque) callconv(.C) void;
pub const move_fn = *const fn (from: ?*anyopaque, to: ?*anyopaque) callconv(.C) void;
pub const alloc_fn = *const fn () callconv(.C) ?*anyopaque;
pub const get_total_size_fn = *const fn (self: ?*const anyopaque) callconv(.C) usize;

pub const type_def_kind = enum(c_int) {
    type_simple = 0,
    type_complex = 1,
};

pub const field_def = extern struct {
    name: ?[*:0]const u8,
    type: ?*const type_def_obj,
    meta: ?*const anyopaque,
};

pub const param_def = extern struct {
    name: ?[*:0]const u8,
    type: ?*const type_def_obj,
    meta: ?*const anyopaque,
};

pub const method_def = extern struct {
    name: ?[*:0]const u8,
    ret: ?*const type_def_obj,
    params: ?*const param_def,
    param_count: c_int,
    meta: ?*const anyopaque,
    impl: ?*anyopaque,
};

pub const type_def_obj = extern struct {
    name: ?[*:0]const u8,
    kind: type_def_kind,
    size: usize,
    init: ?init_fn,
    deleter: ?deleter_fn,
    copy: ?copy_fn,
    move: ?move_fn,
    alloc: ?alloc_fn,
    get_total_size: ?get_total_size_fn,
    fields: ?*const field_def,
    field_count: c_int,
    methods: ?*const method_def,
    method_count: c_int,
};
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

pub export fn new_str(ptr: CStr) CStr {
    ensureInit();
    if (ptr == null) return null;
    const s = mem.span(ptr.?);
    return to_cstr(s) catch null;
}

pub export fn deallocate_str(s: CStr) void {
    ensureInit();
    if (s == null) return;
    const sc = mem.span(s.?);
    g_allocator.free(@constCast(sc));
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
    if (op.typed_sig) |ts| {
        const pc: usize = @intCast(ts.param_count);
        if (pc > 0) {
            const a_count: usize = @intCast(argc);
            if (a_count < pc) return -1;
            if (args != null) {
                const a_ptr = args.?;
                const params = ts.params orelse null;
                if (params != null) {
                    const ps = params.?;
                    var i: usize = 0;
                    while (i < pc) : (i += 1) {
                        const need_tn = if (ps[i].type_name) |tn| cspan(tn) else "value";
                        const a_many: [*]const typed_obj = @ptrCast(a_ptr);
                        const arg_ty = a_many[i].type;
                        if (arg_ty == null) {
                            if (!mem.eql(u8, need_tn, "value") and !mem.eql(u8, need_tn, "any")) return -1;
                        } else {
                            const name_ptr = arg_ty.?.name orelse null;
                            if (name_ptr != null) {
                                const got = cspan(name_ptr.?);
                                if (!mem.eql(u8, need_tn, "any") and !mem.eql(u8, need_tn, "value")) {
                                    if (!mem.eql(u8, got, need_tn)) return -1;
                                }
                            } else {
                                if (!mem.eql(u8, need_tn, "any") and !mem.eql(u8, need_tn, "value")) return -1;
                            }
                        }
                    }
                }
            }
        }
    }
    return op.impl_fn.?(self, args, argc, out);
}

fn ptr_add(p: ?*anyopaque, off: usize) ?*anyopaque {
    if (p == null) return null;
    const base: [*]u8 = @ptrCast(p.?);
    return @ptrCast(base + off);
}

fn cptr_add(p: ?*const anyopaque, off: usize) ?*const anyopaque {
    if (p == null) return null;
    const base: [*]const u8 = @ptrCast(p.?);
    return @ptrCast(base + off);
}

fn call_init_if_any(def: ?*const type_def_obj, self: ?*anyopaque) void {
    if (def == null or self == null) return;
    if (def.?.init != null) def.?.init.?(self);
}

pub export fn default_mv(from: ?*anyopaque, to: ?*anyopaque, s: usize) void {
    if (from == null or to == null or s == 0) return;
    const f: [*]const u8 = @ptrCast(from.?);
    const t: [*]u8 = @ptrCast(to.?);
    @memcpy(t[0..s], f[0..s]);
    @memset(@as([*]u8, @ptrCast(from.?))[0..s], 0);
}

pub export fn default_copy(from: ?*const anyopaque, to: ?*anyopaque, s: usize) void {
    if (from == null or to == null or s == 0) return;
    const f: [*]const u8 = @ptrCast(from.?);
    const t: [*]u8 = @ptrCast(to.?);
    @memcpy(t[0..s], f[0..s]);
}

pub export fn default_delete(obj: ?*anyopaque) void {
    _ = obj;
}

pub export fn type_total_size(def: ?*const type_def_obj, self: ?*const anyopaque) usize {
    if (def == null) return 0;
    if (def.?.get_total_size != null) return def.?.get_total_size.?(self);
    return def.?.size;
}

pub export fn object_mv(def: ?*const type_def_obj, from: ?*anyopaque, to: ?*anyopaque) c_int {
    if (def == null or to == null or from == null) return -1;
    if (def.?.move != null) {
        def.?.move.?(from, to);
        return 0;
    }
    const sz = type_total_size(def, from);
    default_mv(from, to, sz);
    return 0;
}

pub export fn object_copy(def: ?*const type_def_obj, from: ?*const anyopaque, to: ?*anyopaque) c_int {
    if (def == null or to == null or from == null) return -1;
    if (def.?.copy != null) {
        def.?.copy.?(from, to);
        return 0;
    }
    const sz = type_total_size(def, from);
    default_copy(from, to, sz);
    return 0;
}

pub export fn object_delete(def: ?*const type_def_obj, obj: ?*anyopaque) c_int {
    if (def == null or obj == null) return -1;
    if (def.?.deleter != null) {
        def.?.deleter.?(obj);
        return 0;
    }
    const sz = type_total_size(def, obj);
    if (sz > 0) {
        const p: [*]u8 = @ptrCast(obj.?);
        g_allocator.free(p[0..sz]);
    }
    return 0;
}

pub export fn typed_obj_init(o: ?*typed_obj, def: ?*const type_def_obj, ptr: ?*anyopaque) c_int {
    if (o == null or def == null) return -1;
    o.?.type = def;
    o.?.ptr = ptr;
    call_init_if_any(def, o.?.ptr);
    return 0;
}

pub export fn typed_obj_alloc(o: ?*typed_obj, def: ?*const type_def_obj) c_int {
    if (o == null or def == null) return -1;
    var mem_ptr: ?*anyopaque = null;
    if (def.?.alloc != null) {
        mem_ptr = def.?.alloc.?();
    } else {
        const sz = if (def.?.size > 0) def.?.size else 1;
        var buf = g_allocator.alloc(u8, sz) catch return -1;
        @memset(buf[0..sz], 0);
        mem_ptr = @ptrCast(buf.ptr);
    }
    if (mem_ptr == null) return -1;
    o.?.type = def;
    o.?.ptr = mem_ptr;
    call_init_if_any(def, o.?.ptr);
    return 0;
}

pub export fn typed_obj_copy(dst: ?*typed_obj, src: ?*const typed_obj) c_int {
    if (dst == null or src == null or src.?.type == null or src.?.ptr == null) return -1;
    if (dst.?.ptr == null) {
        const sz = type_total_size(src.?.type, src.?.ptr);
        var buf = g_allocator.alloc(u8, sz) catch return -1;
        @memset(buf[0..sz], 0);
        dst.?.ptr = @ptrCast(buf.ptr);
    }
    dst.?.type = src.?.type;
    return object_copy(src.?.type, src.?.ptr, dst.?.ptr);
}

pub export fn typed_obj_move(dst: ?*typed_obj, src: ?*typed_obj) c_int {
    if (dst == null or src == null or src.?.type == null or src.?.ptr == null) return -1;
    if (dst.?.ptr == null) {
        const sz = type_total_size(src.?.type, src.?.ptr);
        var buf = g_allocator.alloc(u8, sz) catch return -1;
        @memset(buf[0..sz], 0);
        dst.?.ptr = @ptrCast(buf.ptr);
    }
    dst.?.type = src.?.type;
    const rc = object_mv(src.?.type, src.?.ptr, dst.?.ptr);
    if (rc == 0) {
        const sz = type_total_size(src.?.type, null);
        const p: [*]u8 = @ptrCast(src.?.ptr.?);
        g_allocator.free(p[0..sz]);
        src.?.ptr = null;
        src.?.type = null;
    }
    return rc;
}

pub export fn typed_obj_delete(o: ?*typed_obj) c_int {
    if (o == null or o.?.type == null or o.?.ptr == null) return -1;
    const rc = object_delete(o.?.type, o.?.ptr);
    o.?.ptr = null;
    o.?.type = null;
    return rc;
}

fn init_noop(self: ?*anyopaque) callconv(.C) void {
    _ = self;
}

fn SimpleDef(comptime name: [:0]const u8, comptime T: type) type_def_obj {
    return .{
        .name = name,
        .kind = .type_simple,
        .size = @sizeOf(T),
        .init = init_noop,
        .deleter = null,
        .copy = null,
        .move = null,
        .alloc = null,
        .get_total_size = null,
        .fields = null,
        .field_count = 0,
        .methods = null,
        .method_count = 0,
    };
}

pub export const i8_def: type_def_obj = SimpleDef("i8", i8);
pub export const u8_def: type_def_obj = SimpleDef("u8", u8);
pub export const i16_def: type_def_obj = SimpleDef("i16", i16);
pub export const u16_def: type_def_obj = SimpleDef("u16", u16);
pub export const i32_def: type_def_obj = SimpleDef("i32", i32);
pub export const u32_def: type_def_obj = SimpleDef("u32", u32);
pub export const i64_def: type_def_obj = SimpleDef("i64", i64);
pub export const u64_def: type_def_obj = SimpleDef("u64", u64);
pub export const f32_def: type_def_obj = SimpleDef("f32", f32);
pub export const f64_def: type_def_obj = SimpleDef("f64", f64);
pub export const bool_def: type_def_obj = SimpleDef("bool", u8);
pub export const char_def: type_def_obj = SimpleDef("char", u8);
pub export const ptr_def: type_def_obj = SimpleDef("ptr", ?*anyopaque);

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

test "typed_obj alloc/copy/move/delete with i64" {
    var a: typed_obj = .{ .ptr = null, .type = null };
    try std.testing.expect(typed_obj_alloc(&a, &i64_def) == 0);
    var b: typed_obj = .{ .ptr = null, .type = null };
    try std.testing.expect(typed_obj_copy(&b, &a) == 0);
    var moved: typed_obj = .{ .ptr = null, .type = null };
    try std.testing.expect(typed_obj_move(&moved, &a) == 0);
    try std.testing.expect(a.ptr == null and a.type == null);
    try std.testing.expect(typed_obj_delete(&b) == 0);
    try std.testing.expect(typed_obj_delete(&moved) == 0);
}
