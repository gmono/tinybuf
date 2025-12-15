#![allow(non_camel_case_types)]
#![allow(improper_ctypes_definitions)]

#[repr(C)]
pub struct buffer { _private: [u8; 0] }
#[repr(C)]
pub struct tinybuf_value { _private: [u8; 0] }
#[repr(C)]
pub struct tinybuf_error { pub errcode: i32, pub msg: *const u8 }
#[repr(C)]
pub struct buf_ref {
    pub base: *const u8,
    pub all_size: i64,
    pub ptr: *const u8,
    pub size: i64,
}
#[repr(C)]
pub enum tinybuf_type {
    tinybuf_null = 0,
    tinybuf_int = 1,
    tinybuf_bool = 2,
    tinybuf_double = 3,
    tinybuf_string = 4,
    tinybuf_map = 5,
    tinybuf_array = 6,
    tinybuf_custom = 7,
    tinybuf_value_ref = 8,
    tinybuf_version = 9,
    tinybuf_versionlist = 10,
    tinybuf_tensor = 11,
    tinybuf_bool_map = 12,
    tinybuf_indexed_tensor = 13,
}

type CONTAIN_HANDLER = Option<extern "C" fn(u64) -> i32>;
type tinybuf_plugin_value_op_fn = Option<unsafe extern "C" fn(*mut tinybuf_value, *const tinybuf_value, *mut tinybuf_value) -> i32>;
type tinybuf_plugin_read_fn = Option<unsafe extern "C" fn(u8, *mut buf_ref, *mut tinybuf_value, CONTAIN_HANDLER, *mut tinybuf_error) -> i32>;
type tinybuf_plugin_write_fn = Option<unsafe extern "C" fn(u8, *const tinybuf_value, *mut buffer, *mut tinybuf_error) -> i32>;
type tinybuf_plugin_dump_fn = Option<unsafe extern "C" fn(u8, *mut buf_ref, *mut buffer, *mut tinybuf_error) -> i32>;
type tinybuf_plugin_show_value_fn = Option<unsafe extern "C" fn(u8, *const tinybuf_value, *mut buffer, *mut tinybuf_error) -> i32>;

#[repr(C)]
pub struct tinybuf_plugin_descriptor {
    pub tags: *const u8,
    pub tag_count: i32,
    pub guid: *const u8,
    pub read: tinybuf_plugin_read_fn,
    pub write: tinybuf_plugin_write_fn,
    pub dump: tinybuf_plugin_dump_fn,
    pub show_value: tinybuf_plugin_show_value_fn,
    pub op_names: *const *const u8,
    pub op_sigs: *const *const u8,
    pub op_descs: *const *const u8,
    pub op_fns: *const tinybuf_plugin_value_op_fn,
    pub op_count: i32,
}

#[link(name = "tinybuf", kind = "static")]
extern "C" {
    fn tinybuf_malloc(size: i32) -> *mut u8;
    fn tinybuf_free(ptr: *mut u8);
    fn tinybuf_value_init_string(v: *mut tinybuf_value, s: *const u8, len: i32) -> i32;
    fn tinybuf_value_get_string(v: *const tinybuf_value, r: *mut tinybuf_error) -> *mut buffer;
    fn tinybuf_plugin_get_runtime_index_by_tag(tag: u8) -> i32;
    fn tinybuf_value_set_plugin_index(v: *mut tinybuf_value, idx: i32) -> i32;
    fn buffer_get_length(b: *mut buffer) -> i32;
    fn buffer_get_data(b: *mut buffer) -> *const u8;
    fn buffer_append(b: *mut buffer, data: *const u8, len: i32) -> i32;
    fn tinybuf_plugin_register_descriptor(d: *const tinybuf_plugin_descriptor) -> i32;
}

const DLL_UPPER_TYPE: u8 = 201;

unsafe extern "C" fn dll_upper_read(
    t: u8,
    buf: *mut buf_ref,
    out: *mut tinybuf_value,
    _contain: CONTAIN_HANDLER,
    r: *mut tinybuf_error,
) -> i32 {
    if t != DLL_UPPER_TYPE { return -1; }
    if (*buf).size < 2 { return 0; }
    let p = (*buf).ptr;
    let tag = *p;
    if tag != DLL_UPPER_TYPE { return -1; }
    let len = *p.add(1) as i32;
    if (*buf).size < (2 + len) as i64 { return 0; }
    let src = p.add(2);
    let tmp = tinybuf_malloc(len);
    for i in 0..len {
        let c = *src.add(i as usize);
        *tmp.add(i as usize) = if c >= b'a' && c <= b'z' { c - b'a' + b'A' } else { c };
    }
    tinybuf_value_init_string(out, tmp, len);
    let idx = tinybuf_plugin_get_runtime_index_by_tag(DLL_UPPER_TYPE);
    tinybuf_value_set_plugin_index(out, idx);
    tinybuf_free(tmp);
    (*buf).ptr = src.add(len as usize);
    (*buf).size -= (2 + len) as i64;
    (2 + len) as i32
}

unsafe extern "C" fn dll_upper_write(
    t: u8,
    input: *const tinybuf_value,
    out: *mut buffer,
    r: *mut tinybuf_error,
) -> i32 {
    if t != DLL_UPPER_TYPE { return -1; }
    let mut er = tinybuf_error { errcode: 0, msg: std::ptr::null() };
    let s = tinybuf_value_get_string(input, &mut er);
    if s.is_null() { return -1; }
    let mut len = buffer_get_length(s);
    if len > 255 { len = 255; }
    let tag: u8 = DLL_UPPER_TYPE;
    buffer_append(out, &tag as *const u8, 1);
    let l: u8 = len as u8;
    buffer_append(out, &l as *const u8, 1);
    buffer_append(out, buffer_get_data(s), len);
    (2 + len) as i32
}

unsafe extern "C" fn dll_upper_dump(
    t: u8,
    buf: *mut buf_ref,
    out: *mut buffer,
    r: *mut tinybuf_error,
) -> i32 {
    if t != DLL_UPPER_TYPE { return -1; }
    if (*buf).size < 2 { return 0; }
    let p = (*buf).ptr;
    if *p != DLL_UPPER_TYPE { return -1; }
    let len = *p.add(1) as i32;
    if (*buf).size < (2 + len) as i64 { return 0; }
    buffer_append(out, "\"".as_ptr(), 1);
    let src = p.add(2);
    for i in 0..len {
        let mut c = *src.add(i as usize);
        if c >= b'a' && c <= b'z' { c = c - b'a' + b'A'; }
        buffer_append(out, &c as *const u8, 1);
    }
    buffer_append(out, "\"".as_ptr(), 1);
    (2 + len) as i32
}

unsafe extern "C" fn dll_upper_show_value(
    t: u8,
    input: *const tinybuf_value,
    out: *mut buffer,
    r: *mut tinybuf_error,
) -> i32 {
    if t != DLL_UPPER_TYPE { return -1; }
    let mut er = tinybuf_error { errcode: 0, msg: std::ptr::null() };
    let s = tinybuf_value_get_string(input, &mut er);
    if s.is_null() { return -1; }
    buffer_append(out, "dll_upper(".as_ptr(), 10);
    buffer_append(out, buffer_get_data(s), buffer_get_length(s));
    buffer_append(out, ")".as_ptr(), 1);
    buffer_get_length(s) + 11
}

unsafe extern "C" fn dll_to_lower(value: *mut tinybuf_value, _args: *const tinybuf_value, out: *mut tinybuf_value) -> i32 {
    let mut er = tinybuf_error { errcode: 0, msg: std::ptr::null() };
    let s = tinybuf_value_get_string(value, &mut er);
    if s.is_null() { return -1; }
    let len = buffer_get_length(s);
    let src = buffer_get_data(s);
    let tmp = tinybuf_malloc(len);
    for i in 0..len {
        let c = *src.add(i as usize);
        *tmp.add(i as usize) = if c >= b'A' && c <= b'Z' { c + (b'a' - b'A') } else { c };
    }
    tinybuf_value_init_string(out, tmp, len);
    tinybuf_free(tmp);
    0
}

static mut TAGS: [u8; 1] = [DLL_UPPER_TYPE];
static mut OP_NAMES: [*const u8; 1] = [b"to_lower\0".as_ptr()];
static mut OP_SIGS: [*const u8; 1] = [b"string->string\0".as_ptr()];
static mut OP_DESCS: [*const u8; 1] = [b"lowercase\0".as_ptr()];
static mut OP_FNS: [tinybuf_plugin_value_op_fn; 1] = [Some(dll_to_lower)];
static mut DESC: tinybuf_plugin_descriptor = tinybuf_plugin_descriptor {
    tags: std::ptr::null(),
    tag_count: 0,
    guid: std::ptr::null(),
    read: None,
    write: None,
    dump: None,
    show_value: None,
    op_names: std::ptr::null(),
    op_sigs: std::ptr::null(),
    op_descs: std::ptr::null(),
    op_fns: std::ptr::null(),
    op_count: 0,
};

#[no_mangle]
pub unsafe extern "C" fn tinybuf_get_plugin_descriptor() -> *mut tinybuf_plugin_descriptor {
    DESC.tags = TAGS.as_ptr();
    DESC.tag_count = 1;
    DESC.guid = b"dll:upper-string\0".as_ptr();
    DESC.read = Some(dll_upper_read);
    DESC.write = Some(dll_upper_write);
    DESC.dump = Some(dll_upper_dump);
    DESC.show_value = Some(dll_upper_show_value);
    DESC.op_names = OP_NAMES.as_ptr();
    DESC.op_sigs = OP_SIGS.as_ptr();
    DESC.op_descs = OP_DESCS.as_ptr();
    DESC.op_fns = OP_FNS.as_ptr();
    DESC.op_count = 1;
    &mut DESC
}

pub fn get_desc_ptr() -> *mut tinybuf_plugin_descriptor {
    unsafe { tinybuf_get_plugin_descriptor() }
}

pub fn register_self() -> i32 {
    unsafe { tinybuf_plugin_register_descriptor(tinybuf_get_plugin_descriptor()) }
}
