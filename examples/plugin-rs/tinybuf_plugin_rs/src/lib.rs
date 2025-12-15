#[allow(non_camel_case_types)]
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
}

#[repr(C)]
pub struct buffer {
    _private: [u8; 0],
}
#[repr(C)]
pub struct tinybuf_value {
    _private: [u8; 0],
}

#[repr(C)]
pub struct tinybuf_plugin_descriptor {
    pub tags: *const u8,
    pub tag_count: i32,
    pub guid: *const u8,
    pub read: Option<unsafe extern "C" fn(u8, *mut buf_ref, *mut tinybuf_value, Option<extern "C" fn(u64) -> i32>, *mut tinybuf_error) -> i32>,
    pub write: Option<unsafe extern "C" fn(u8, *const tinybuf_value, *mut buffer, *mut tinybuf_error) -> i32>,
    pub dump: Option<unsafe extern "C" fn(u8, *mut buf_ref, *mut buffer, *mut tinybuf_error) -> i32>,
    pub show_value: Option<unsafe extern "C" fn(u8, *const tinybuf_value, *mut buffer, *mut tinybuf_error) -> i32>,
    pub op_names: *const *const u8,
    pub op_sigs: *const *const u8,
    pub op_descs: *const *const u8,
    pub op_fns: *const Option<unsafe extern "C" fn(*mut tinybuf_value, *const tinybuf_value, *mut tinybuf_value) -> i32>,
    pub op_count: i32,
}
#[repr(C)]
pub struct buf_ref {
    pub begin: *const u8,
    pub length: i64,
    pub cur: *const u8,
    pub cur_length: i64,
}
#[repr(C)]
pub struct tinybuf_error {
    pub errcode: i32,
    pub msg: *const u8,
}

#[link(name = "tinybuf", kind = "static")]
extern "C" {
    pub fn tinybuf_init() -> i32;
    pub fn tinybuf_value_alloc() -> *mut tinybuf_value;
    pub fn tinybuf_value_free(v: *mut tinybuf_value) -> i32;
    pub fn tinybuf_value_init_int(v: *mut tinybuf_value, x: i64) -> i32;
    pub fn tinybuf_value_init_string(v: *mut tinybuf_value, s: *const u8, len: i32) -> i32;
    pub fn tinybuf_value_alloc_with_type(t: tinybuf_type) -> *mut tinybuf_value;
    pub fn tinybuf_value_array_append(parent: *mut tinybuf_value, child: *mut tinybuf_value) -> i32;
    pub fn tinybuf_value_get_type(v: *const tinybuf_value) -> tinybuf_type;
    pub fn buffer_alloc() -> *mut buffer;
    pub fn buffer_free(b: *mut buffer) -> i32;
    pub fn buffer_get_data(b: *mut buffer) -> *const u8;
    pub fn buffer_get_length(b: *mut buffer) -> i64;

    pub fn tinybuf_plugin_register_from_dll(path: *const u8) -> i32;
    pub fn tinybuf_plugin_get_count() -> i32;
    pub fn tinybuf_plugin_get_guid(index: i32) -> *const u8;
    pub fn tinybuf_plugin_do_value_op_by_tag(tag: u8, name: *const u8, value: *mut tinybuf_value, args: *const tinybuf_value, out: *mut tinybuf_value) -> i32;
    pub fn tinybuf_value_get_string(v: *const tinybuf_value, r: *mut tinybuf_error) -> *mut buffer;
    pub fn tinybuf_plugin_register_descriptor(d: *const tinybuf_plugin_descriptor) -> i32;
}

pub fn load_plugin(path: &str) -> i32 {
    let p = std::ffi::CString::new(path).unwrap();
    unsafe { tinybuf_plugin_register_from_dll(p.as_ptr() as *const u8) }
}
pub fn init() { unsafe { tinybuf_init(); } }
pub fn plugin_count() -> i32 { unsafe { tinybuf_plugin_get_count() } }
pub fn plugin_guid(i: i32) -> Option<String> {
    unsafe {
        let p = tinybuf_plugin_get_guid(i);
        if p.is_null() { None } else {
            Some(std::ffi::CStr::from_ptr(p as *const i8).to_string_lossy().into_owned())
        }
    }
}
