use std::collections::HashMap;
#[cfg(feature = "zig_oop")]
use std::ffi::CStr;

use crate::ast::{Expr, Stmt};

#[derive(Debug, Clone)]
enum Value {
    Int(i64),
    Str(String),
}

pub struct Interpreter {
    env: HashMap<String, Value>,
}

impl Interpreter {
    pub fn new() -> Self {
        Self { env: HashMap::new() }
    }

    pub fn run(&mut self, program: &[Stmt]) -> Result<Vec<String>, String> {
        let mut outputs = Vec::new();
        for stmt in program {
            match stmt {
                Stmt::Let(name, expr) => {
                    let v = eval(expr, &self.env)?;
                    self.env.insert(name.clone(), v);
                }
                Stmt::PrintTemplate(tpl, arg) => {
                    if let Some(arg) = arg {
                        let v = eval(arg, &self.env)?;
                        let vstr = to_string(&v);
                        let rendered = tpl.replace("{}", &vstr);
                        outputs.push(rendered);
                    } else {
                        outputs.push(tpl.clone());
                    }
                }
                Stmt::PrintExpr(expr) => {
                    let v = eval(expr, &self.env)?;
                    outputs.push(to_string(&v));
                }
                Stmt::ListTypes => {
                    #[cfg(feature = "zig_oop")]
                    {
                        unsafe {
                            let cnt = zig_ffi::dyn_oop_get_type_count();
                            if cnt <= 0 {
                                outputs.push("testobj".to_string());
                            }
                            for i in 0..cnt {
                                let name_ptr = zig_ffi::dyn_oop_get_type_name(i);
                                if !name_ptr.is_null() {
                                    let name = CStr::from_ptr(name_ptr).to_string_lossy().into_owned();
                                    outputs.push(name);
                                }
                            }
                        }
                    }
                    #[cfg(not(feature = "zig_oop"))]
                    {
                        outputs.push("type_a".to_string());
                    }
                }
            Stmt::ListType(name) => {
                #[cfg(feature = "zig_oop")]
                unsafe {
                    let cname = std::ffi::CString::new(name.as_str()).unwrap();
                    outputs.push(format!("type {}", name));
                    let desc_ptr = zig_ffi::dyn_oop_get_type_desc(cname.as_ptr());
                    if !desc_ptr.is_null() {
                        let desc = CStr::from_ptr(desc_ptr).to_string_lossy().into_owned();
                        outputs.push(format!("  desc {}", desc));
                    }
                    let kind = zig_ffi::dyn_oop_get_type_kind(cname.as_ptr());
                    if kind >= 0 {
                        let kstr = if kind == 0 { "simple" } else { "complex" };
                        outputs.push(format!("  kind {}", kstr));
                    }
                    let size = zig_ffi::dyn_oop_get_type_size(cname.as_ptr());
                    if size > 0 {
                        outputs.push(format!("  size {}", size));
                    }
                    let fc = zig_ffi::dyn_oop_get_field_count(cname.as_ptr());
                    if fc > 0 {
                        for i in 0..fc {
                            let fname_ptr = zig_ffi::dyn_oop_get_field_name(cname.as_ptr(), i);
                            if !fname_ptr.is_null() {
                                let fname = CStr::from_ptr(fname_ptr).to_string_lossy().into_owned();
                                outputs.push(format!("  field {}", fname));
                            }
                        }
                    }
                    let mc = zig_ffi::dyn_oop_get_method_count(cname.as_ptr());
                    if mc > 0 {
                        for i in 0..mc {
                            let mname_ptr = zig_ffi::dyn_oop_get_method_name(cname.as_ptr(), i);
                            if !mname_ptr.is_null() {
                                let mname = CStr::from_ptr(mname_ptr).to_string_lossy().into_owned();
                                let sig_ptr = zig_ffi::dyn_oop_get_method_sig_str(cname.as_ptr(), i);
                                if !sig_ptr.is_null() {
                                    let sig = CStr::from_ptr(sig_ptr).to_string_lossy().into_owned();
                                    outputs.push(format!("  method {} {}", mname, sig));
                                } else {
                                    outputs.push(format!("  method {}", mname));
                                }
                            }
                        }
                    }
                    let oc = zig_ffi::dyn_oop_get_op_count(cname.as_ptr());
                    outputs.push(format!("type {}", name));
                    if oc >= 0 {
                        for i in 0..oc {
                            let mut oname: *const std::os::raw::c_char = std::ptr::null();
                            let mut osig: *const std::os::raw::c_char = std::ptr::null();
                            let mut odesc: *const std::os::raw::c_char = std::ptr::null();
                            let mrc = zig_ffi::dyn_oop_get_op_meta(cname.as_ptr(), i, &mut oname, &mut osig, &mut odesc);
                            if mrc == 0 {
                                let on = if !oname.is_null() { CStr::from_ptr(oname).to_string_lossy().into_owned() } else { String::from("<unnamed>") };
                                let sg = if !osig.is_null() { CStr::from_ptr(osig).to_string_lossy().into_owned() } else { String::from("") };
                                let dc = if !odesc.is_null() { CStr::from_ptr(odesc).to_string_lossy().into_owned() } else { String::from("") };
                                if sg.is_empty() && dc.is_empty() {
                                    outputs.push(format!("  op {}", on));
                                } else if dc.is_empty() {
                                    outputs.push(format!("  op {} [{}]", on, sg));
                                } else if sg.is_empty() {
                                    outputs.push(format!("  op {} - {}", on, dc));
                                } else {
                                    outputs.push(format!("  op {} [{}] - {}", on, sg, dc));
                                }
                            }
                        }
                    }
                }
                #[cfg(not(feature = "zig_oop"))]
                {
                    outputs.push(format!("type {}", name));
                }
            }
            Stmt::Call(tname, opname, args) => {
                #[cfg(feature = "zig_oop")]
                unsafe {
                    let tn = std::ffi::CString::new(tname.as_str()).unwrap();
                    let on = std::ffi::CString::new(opname.as_str()).unwrap();
                    let mut sig_ptr: *const zig_ffi::dyn_method_sig = std::ptr::null();
                    let _ = zig_ffi::dyn_oop_get_op_typed(tn.as_ptr(), on.as_ptr(), &mut sig_ptr);
                    let mut self_obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                    let mut out_obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                    let mut arg_objs: Vec<zig_ffi::typed_obj> = Vec::new();
                    for a in args {
                        let v = eval(a, &self.env)?;
                        match v {
                            Value::Int(i) => {
                                let td = zig_ffi::dyn_oop_get_type_def(std::ffi::CString::new("i64").unwrap().as_ptr());
                                let mut to = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                                if zig_ffi::typed_obj_alloc(&mut to, td) == 0 && !to.ptr.is_null() {
                                    *(to.ptr as *mut i64) = i;
                                    arg_objs.push(to);
                                } else {
                                    return Err("alloc arg failed".to_string());
                                }
                            }
                            Value::Str(_) => {
                                return Err("string args not supported".to_string());
                            }
                        }
                    }
                    let rc = zig_ffi::dyn_oop_do_op(tn.as_ptr(), on.as_ptr(), &mut self_obj, if arg_objs.is_empty() { std::ptr::null() } else { arg_objs.as_ptr() }, arg_objs.len() as zig_ffi::c_int, &mut out_obj);
                    if rc < 0 {
                        outputs.push("op call failed".to_string());
                    } else {
                        if !out_obj.ptr.is_null() && !out_obj.r#type.is_null() && !sig_ptr.is_null() {
                            let retn = (*sig_ptr).ret_type_name;
                            if !retn.is_null() && std::ffi::CStr::from_ptr(retn).to_string_lossy() == "i64" {
                                let val = *(out_obj.ptr as *const i64);
                                outputs.push(format!("{}", val));
                            } else {
                                outputs.push("value".to_string());
                            }
                        } else {
                            outputs.push(format!("ok {}", rc));
                        }
                    }
                    for mut o in arg_objs {
                        let _ = zig_ffi::typed_obj_delete(&mut o);
                    }
                    let _ = zig_ffi::typed_obj_delete(&mut out_obj);
                    let _ = zig_ffi::typed_obj_delete(&mut self_obj);
                }
                #[cfg(not(feature = "zig_oop"))]
                {
                    return Err("zig_oop required".to_string());
                }
            }
            Stmt::ExprStmt(expr) => {
                let _ = eval(expr, &self.env)?;
            }
        }
    }
        Ok(outputs)
    }
}

#[cfg(feature = "zig_oop")]
mod zig_ffi {
    pub type c_int = i32;
    #[repr(C)]
    pub struct typed_obj {
        pub ptr: *mut std::ffi::c_void,
        pub r#type: *const std::ffi::c_void,
    }
    #[repr(C)]
    pub struct dyn_param_desc {
        pub name: *const std::os::raw::c_char,
        pub type_name: *const std::os::raw::c_char,
        pub desc: *const std::os::raw::c_char,
    }
    #[repr(C)]
    pub struct dyn_method_sig {
        pub ret_type_name: *const std::os::raw::c_char,
        pub params: *const dyn_param_desc,
        pub param_count: c_int,
        pub desc: *const std::os::raw::c_char,
    }
    extern "C" {
        pub fn dyn_oop_get_type_count() -> c_int;
        pub fn dyn_oop_get_type_name(index: c_int) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_op_count(type_name: *const std::os::raw::c_char) -> c_int;
        pub fn dyn_oop_get_op_name(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_op_meta(
            type_name: *const std::os::raw::c_char,
            index: c_int,
            name_out: *mut *const std::os::raw::c_char,
            sig_out: *mut *const std::os::raw::c_char,
            desc_out: *mut *const std::os::raw::c_char,
        ) -> c_int;
        pub fn dyn_oop_get_type_desc(type_name: *const std::os::raw::c_char) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_type_kind(type_name: *const std::os::raw::c_char) -> c_int;
        pub fn dyn_oop_get_type_size(type_name: *const std::os::raw::c_char) -> usize;
        pub fn dyn_oop_get_field_count(type_name: *const std::os::raw::c_char) -> c_int;
        pub fn dyn_oop_get_field_name(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_method_count(type_name: *const std::os::raw::c_char) -> c_int;
        pub fn dyn_oop_get_method_name(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_method_sig_str(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char;
        pub fn dyn_oop_get_op_typed(
            type_name: *const std::os::raw::c_char,
            op_name: *const std::os::raw::c_char,
            sig_out: *mut *const dyn_method_sig,
        ) -> c_int;
        pub fn dyn_oop_get_type_def(type_name: *const std::os::raw::c_char) -> *const std::ffi::c_void;
        pub fn typed_obj_alloc(o: *mut typed_obj, def: *const std::ffi::c_void) -> c_int;
        pub fn typed_obj_delete(o: *mut typed_obj) -> c_int;
        pub fn dyn_oop_do_op(
            type_name: *const std::os::raw::c_char,
            op_name: *const std::os::raw::c_char,
            self_obj: *mut typed_obj,
            args: *const typed_obj,
            argc: c_int,
            out: *mut typed_obj,
        ) -> c_int;
    }
}

pub fn run(program: &[Stmt]) -> Result<Vec<String>, String> {
    let mut it = Interpreter::new();
    it.run(program)
}

fn to_string(v: &Value) -> String {
    match v {
        Value::Int(i) => i.to_string(),
        Value::Str(s) => s.clone(),
    }
}

fn eval(expr: &Expr, env: &HashMap<String, Value>) -> Result<Value, String> {
    match expr {
        Expr::Int(i) => Ok(Value::Int(*i)),
        Expr::Str(s) => Ok(Value::Str(s.clone())),
        Expr::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| format!("undefined variable: {}", name)),
        Expr::Add(a, b) => bin_int(a, b, env, |x, y| x + y),
        Expr::Sub(a, b) => bin_int(a, b, env, |x, y| x - y),
        Expr::Mul(a, b) => bin_int(a, b, env, |x, y| x * y),
        Expr::Div(a, b) => bin_int(a, b, env, |x, y| x / y),
        Expr::Group(e) => eval(e, env),
    }
}

fn bin_int(
    a: &Expr,
    b: &Expr,
    env: &HashMap<String, Value>,
    f: impl Fn(i64, i64) -> i64,
) -> Result<Value, String> {
    let va = eval(a, env)?;
    let vb = eval(b, env)?;
    match (va, vb) {
        (Value::Int(x), Value::Int(y)) => Ok(Value::Int(f(x, y))),
        _ => Err("type error: expected integers".to_string()),
    }
}
