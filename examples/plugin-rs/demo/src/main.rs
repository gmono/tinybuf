use tinybuf_plugin_rs::*;

fn main() {
    init();
    // 直接注册Rust端插件描述符，验证“用Rust写C核心插件”的接口
    let r = upper_plugin_rs::register_self();
    println!("register ret={}", r);
    let cnt = plugin_count();
    println!("plugin count={}", cnt);
    for i in 0..cnt {
        println!("plugin[{}]={:?}", i, plugin_guid(i));
    }
    #[cfg(target_os = "windows")]
    {
        unsafe {
            let v = tinybuf_value_alloc();
            let s = std::ffi::CString::new("ABC").unwrap();
            tinybuf_value_init_string(v, s.as_ptr() as *const u8, 3);
            let out = tinybuf_value_alloc();
            let rc = tinybuf_plugin_do_value_op_by_tag(201u8, std::ffi::CString::new("to_lower").unwrap().as_ptr() as *const u8, v, std::ptr::null(), out);
            println!("op rc={}", rc);
            let mut er = tinybuf_error { errcode: 0, msg: std::ptr::null() };
            let buf = tinybuf_value_get_string(out, &mut er);
            if !buf.is_null() {
                let data = buffer_get_data(buf);
                let len = buffer_get_length(buf) as usize;
                let slice = std::slice::from_raw_parts(data, len);
                println!("to_lower -> {}", String::from_utf8_lossy(slice));
            }
            tinybuf_value_free(out);
            tinybuf_value_free(v);
        }
    }
}
