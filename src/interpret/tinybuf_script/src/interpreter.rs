use std::collections::HashMap;
use std::ffi::CStr;
use std::rc::Rc;
use std::fmt;

use crate::ast::{Expr, Stmt};
// use crate::ast::ListItem;

pub struct OopObject {
    pub obj: zig_ffi::typed_obj,
    pub type_name: String,
}

impl Drop for OopObject {
    fn drop(&mut self) {
        unsafe {
            if !self.obj.ptr.is_null() {
                zig_ffi::typed_obj_delete(&mut self.obj);
            }
        }
    }
}

impl fmt::Debug for OopObject {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<OopObject {:?}>", self.obj.ptr)
    }
}

#[derive(Clone)]
pub struct NativeFunc {
    pub params: Vec<(Option<String>, String)>,
    pub body: Rc<dyn Fn(&[Value], &HashMap<String, Value>) -> Result<Value, String>>,
}

impl fmt::Debug for NativeFunc {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "<NativeFunc>")
    }
}

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Str(String),
    Sym(String),
    List(Vec<Value>, std::collections::HashMap<String, usize>),
    Func(Vec<(Option<String>, String)>, Expr, HashMap<String, Value>),
    FuncBlock(Vec<(Option<String>, String)>, Vec<Stmt>, HashMap<String, Value>),
    NativeFunc(NativeFunc),
    Object(Rc<OopObject>),
}

fn check_args(params: &[(Option<String>, String)], args: &[Value]) -> Result<(), String> {
    if args.len() != params.len() {
        return Err(format!("arity mismatch: expected {}, got {}", params.len(), args.len()));
    }
    for (i, (param_type, _)) in params.iter().enumerate() {
        if let Some(pt) = param_type {
            match (pt.as_str(), &args[i]) {
                ("int", Value::Int(_)) => {},
                ("str", Value::Str(_)) => {},
                ("sym", Value::Sym(_)) => {},
                ("list", Value::List(_, _)) => {},
                ("obj", Value::Object(_)) => {},
                ("any", _) => {},
                (t, v) => return Err(format!("type mismatch for arg {}: expected {}, got {}", i, t, to_string(v))),
            }
        }
    }
    Ok(())
}

fn apply_func(func: &Value, args: &[Value], ops: &HashMap<String, String>, env: &HashMap<String, Value>) -> Result<Value, String> {
    match func {
        Value::Func(params, body, fenv) => {
            check_args(params, args)?;
            let mut call_env = fenv.clone();
            for (p, v) in params.iter().zip(args.iter()) {
                call_env.insert(p.1.clone(), v.clone());
            }
            eval(body, &call_env, ops)
        }
        Value::FuncBlock(params, body_stmts, fenv) => {
            check_args(params, args)?;
            let mut call_env = fenv.clone();
            for (p, v) in params.iter().zip(args.iter()) {
                call_env.insert(p.1.clone(), v.clone());
            }
            eval_block(body_stmts, &mut call_env, ops)
        }
        Value::NativeFunc(nf) => {
            check_args(&nf.params, args)?;
            (nf.body)(args, env)
        }
        _ => Err("not a function".to_string()),
    }
}

pub struct Interpreter {
    env: HashMap<String, Value>,
    ops: HashMap<String, String>,
}

impl Interpreter {
    pub fn new() -> Self {
        init_oop_demo_once();
        let mut env = HashMap::new();
        
        let init_body = Rc::new(|args: &[Value], _env: &HashMap<String, Value>| -> Result<Value, String> {
             let type_name = match &args[0] {
                 Value::Sym(s) => s,
                 _ => return Err("init expects a symbol".to_string()),
             };
             unsafe {
                 let cname = std::ffi::CString::new(type_name.as_str()).map_err(|_| "invalid type name")?;
                 let def = zig_ffi::dyn_oop_get_type_def(cname.as_ptr());
                 if def.is_null() {
                     return Err(format!("type not found: {}", type_name));
                 }
                 let mut obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                 if zig_ffi::typed_obj_alloc(&mut obj, def) != 0 {
                     return Err("alloc failed".to_string());
                 }
                 Ok(Value::Object(Rc::new(OopObject { obj, type_name: type_name.clone() })))
             }
        });

        env.insert("init".to_string(), Value::NativeFunc(NativeFunc {
            params: vec![(Some("sym".to_string()), "type_name".to_string())],
            body: init_body,
        }));
        
        // Reflection: typeof(val) -> str
        let type_body = Rc::new(|args: &[Value], _env: &HashMap<String, Value>| -> Result<Value, String> {
            let v = &args[0];
            match v {
                Value::Int(_) => Ok(Value::Str("int".to_string())),
                Value::Str(_) => Ok(Value::Str("str".to_string())),
                Value::Sym(_) => Ok(Value::Str("sym".to_string())),
                Value::List(_, _) => Ok(Value::Str("list".to_string())),
                Value::Func(_, _, _) | Value::FuncBlock(_, _, _) | Value::NativeFunc(_) => Ok(Value::Str("func".to_string())),
                Value::Object(o) => Ok(Value::Str(o.type_name.clone())),
            }
        });
        env.insert("typeof".to_string(), Value::NativeFunc(NativeFunc {
            params: vec![(Some("any".to_string()), "val".to_string())],
            body: type_body,
        }));

        // Reflection: val(sym) -> value
        let val_body = Rc::new(|args: &[Value], env: &HashMap<String, Value>| -> Result<Value, String> {
             let var_name = match &args[0] {
                 Value::Sym(s) => s,
                 _ => return Err("val expects a symbol".to_string()),
             };
             env.get(var_name).cloned().ok_or_else(|| format!("undefined variable: {}", var_name))
        });
        env.insert("val".to_string(), Value::NativeFunc(NativeFunc {
            params: vec![(Some("sym".to_string()), "var".to_string())],
            body: val_body,
        }));

        // Reflection: sys_types() -> list<str>
        let sys_types_body = Rc::new(|_args: &[Value], _env: &HashMap<String, Value>| -> Result<Value, String> {
            let mut types = Vec::new();
            unsafe {
                let cnt = zig_ffi::dyn_oop_get_type_count();
                for i in 0..cnt {
                    let name_ptr = zig_ffi::dyn_oop_get_type_name(i);
                    if !name_ptr.is_null() {
                        let name = CStr::from_ptr(name_ptr).to_string_lossy().into_owned();
                        types.push(Value::Str(name));
                    }
                }
            }
            Ok(Value::List(types, HashMap::new()))
        });
        env.insert("sys_types".to_string(), Value::NativeFunc(NativeFunc {
            params: vec![],
            body: sys_types_body,
        }));

        // Reflection: sys_methods(sym) -> list<str>
        let sys_methods_body = Rc::new(|args: &[Value], _env: &HashMap<String, Value>| -> Result<Value, String> {
            let type_name = match &args[0] {
                Value::Sym(s) => s,
                _ => return Err("sys_methods expects a symbol type name".to_string()),
            };
            let mut methods = Vec::new();
            unsafe {
                let cname = std::ffi::CString::new(type_name.as_str()).map_err(|_| "invalid type name")?;
                let mc = zig_ffi::dyn_oop_get_method_count(cname.as_ptr());
                for i in 0..mc {
                    let mname_ptr = zig_ffi::dyn_oop_get_method_name(cname.as_ptr(), i);
                    if !mname_ptr.is_null() {
                        let mname = CStr::from_ptr(mname_ptr).to_string_lossy().into_owned();
                        methods.push(Value::Str(mname));
                    }
                }
                // Also get operators as they are methods
                let oc = zig_ffi::dyn_oop_get_op_count(cname.as_ptr());
                for i in 0..oc {
                     let mut oname: *const std::os::raw::c_char = std::ptr::null();
                     let mut osig: *const std::os::raw::c_char = std::ptr::null();
                     let mut odesc: *const std::os::raw::c_char = std::ptr::null();
                     let mrc = zig_ffi::dyn_oop_get_op_meta(cname.as_ptr(), i, &mut oname, &mut osig, &mut odesc);
                     if mrc == 0 && !oname.is_null() {
                         let on = CStr::from_ptr(oname).to_string_lossy().into_owned();
                         methods.push(Value::Str(on));
                     }
                }
            }
            Ok(Value::List(methods, HashMap::new()))
        });
        env.insert("sys_methods".to_string(), Value::NativeFunc(NativeFunc {
            params: vec![(Some("sym".to_string()), "type_name".to_string())],
            body: sys_methods_body,
        }));

        // sym(str) -> sym
        let sym_body = Rc::new(|args: &[Value], _env: &HashMap<String, Value>| -> Result<Value, String> {
             match &args[0] {
                 Value::Str(s) => Ok(Value::Sym(s.clone())),
                 _ => Err("sym expects string".to_string()),
             }
        });
        env.insert("sym".to_string(), Value::NativeFunc(NativeFunc {
             params: vec![(Some("str".to_string()), "s".to_string())],
             body: sym_body,
        }));

        Self { env, ops: HashMap::new() }
    }

    fn run_list_stmt(&mut self, items: &[Expr], outputs: &mut Vec<String>) -> Result<(), String> {
        if items.is_empty() { return Ok(()); }
        
        let head = &items[0];
        let head_sym = match head {
            Expr::Sym(s) => Some(s.as_str()),
            _ => None,
        };
        
        if let Some(s) = head_sym {
            match s {
                "let" => {
                    if items.len() < 3 { return Err("let requires name and value".to_string()); }
                     let name = match &items[1] {
                         Expr::Sym(n) | Expr::Var(n) => n.clone(),
                         _ => return Err("let expects symbol/var name".to_string()),
                     };
                     let val = eval(&items[2], &self.env, &self.ops)?;
                     self.env.insert(name, val);
                     return Ok(());
                }
                "print" => {
                    for arg in items.iter().skip(1) {
                        let v = eval(arg, &self.env, &self.ops)?;
                        outputs.push(to_string(&v));
                    }
                    return Ok(());
                }
                "run" => {
                     if items.len() < 2 { return Err("run requires list".to_string()); }
                     let v = eval(&items[1], &self.env, &self.ops)?;
                     if let Value::List(sub_items, _) = v {
                          return self.run_list_values(&sub_items, outputs);
                     }
                     return Err("run expects a list value".to_string());
                }
                _ => {}
            }
        }
        
        let list_expr = Expr::List(items.iter().map(|e| ListItem { key: None, value: e.clone() }).collect());
        let res = eval(&list_expr, &self.env, &self.ops)?;
        outputs.push(to_string(&res));
        Ok(())
    }

    fn run_list_values(&mut self, items: &[Value], outputs: &mut Vec<String>) -> Result<(), String> {
        if items.is_empty() { return Ok(()); }
        
        let head = &items[0];
        let is_special = match head {
            Value::Sym(s) => matches!(s.as_str(), "let" | "print" | "run"),
            _ => false
        };
        
        if is_special {
             if let Value::Sym(s) = head {
                 match s.as_str() {
                     "let" => {
                          if items.len() < 3 { return Err("let requires name and value".to_string()); }
                          let name = match &items[1] {
                              Value::Sym(n) | Value::Str(n) => n.clone(),
                              _ => return Err("let expects symbol name".to_string()),
                          };
                          let val = items[2].clone();
                          self.env.insert(name, val);
                          return Ok(());
                     }
                     "print" => {
                         for arg in items.iter().skip(1) {
                             outputs.push(to_string(arg));
                         }
                         return Ok(());
                     }
                     "run" => {
                          if items.len() < 2 { return Err("run requires list".to_string()); }
                          if let Value::List(sub, _) = &items[1] {
                              return self.run_list_values(sub, outputs);
                          }
                          return Err("run expects list".to_string());
                     }
                     _ => {}
                 }
             }
        }
        
        let func = match &items[0] {
            Value::Sym(s) => self.env.get(s).cloned().ok_or_else(|| format!("undefined function: {}", s))?,
            v => v.clone(),
        };
        
        let args = items[1..].to_vec();
        let res = apply_func(&func, &args, &self.ops, &self.env)?;
        outputs.push(to_string(&res));
        Ok(())
    }

    pub fn run(&mut self, program: &[Stmt]) -> Result<Vec<String>, String> {
        let mut outputs = Vec::new();
        for stmt in program {
            match stmt {
                Stmt::RunList(items) => {
                    self.run_list_stmt(items, &mut outputs)?;
                }
                Stmt::Let(name, expr) => {
                    let v = eval(expr, &self.env, &self.ops)?;
                    self.env.insert(name.clone(), v);
                }
                Stmt::LetFunc(name, params, body) => {
                    let func_env = self.env.clone();
                    let v = Value::Func(params.clone(), (*body).clone(), func_env);
                    self.env.insert(name.clone(), v);
                }
                Stmt::LetFuncBlock(name, params, body) => {
                    let func_env = self.env.clone();
                    let v = Value::FuncBlock(params.clone(), body.clone(), func_env);
                    self.env.insert(name.clone(), v);
                }
                Stmt::PrintTemplate(tpl, arg) => {
                    if let Some(arg) = arg {
                        let v = eval(arg, &self.env, &self.ops)?;
                        let vstr = to_string(&v);
                        let rendered = tpl.replace("{}", &vstr);
                        outputs.push(rendered);
                    } else {
                        outputs.push(tpl.clone());
                    }
                }
                Stmt::PrintExpr(expr) => {
                    let v = eval(expr, &self.env, &self.ops)?;
                    outputs.push(to_string(&v));
                }
                Stmt::RegOp(op, fname) => {
                    self.ops.insert(op.clone(), fname.clone());
                }
            Stmt::ListTypes => {
                unsafe {
                    let cnt = zig_ffi::dyn_oop_get_type_count();
                    if cnt <= 0 {
                        outputs.push("no types".to_string());
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
            Stmt::ListType(name) => {
                unsafe {
                    let cname = std::ffi::CString::new(name.as_str()).unwrap();
                    outputs.push(format!("type {}", name));
                    let desc_ptr = zig_ffi::dyn_oop_get_type_desc(cname.as_ptr());
                    if !desc_ptr.is_null() {
                        let desc = CStr::from_ptr(desc_ptr).to_string_lossy().into_owned();
                        outputs.push(format!("  desc {}", desc));
                    } else {
                        outputs.push("  desc <none>".to_string());
                    }
                    let kind = zig_ffi::dyn_oop_get_type_kind(cname.as_ptr());
                    if kind >= 0 {
                        let kstr = if kind == 0 { "simple" } else { "complex" };
                        outputs.push(format!("  kind {}", kstr));
                    } else {
                        outputs.push("  kind <unknown>".to_string());
                    }
                    let size = zig_ffi::dyn_oop_get_type_size(cname.as_ptr());
                    outputs.push(format!("  size {}", size));
                    let fc = zig_ffi::dyn_oop_get_field_count(cname.as_ptr());
                    if fc > 0 {
                        for i in 0..fc {
                            let fname_ptr = zig_ffi::dyn_oop_get_field_name(cname.as_ptr(), i);
                            if !fname_ptr.is_null() {
                                let fname = CStr::from_ptr(fname_ptr).to_string_lossy().into_owned();
                                outputs.push(format!("  field {}", fname));
                            }
                        }
                    } else {
                        outputs.push("  fields <none>".to_string());
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
                    } else {
                        outputs.push("  methods <none>".to_string());
                    }
                    let oc = zig_ffi::dyn_oop_get_op_count(cname.as_ptr());
                    if oc < 0 {
                        outputs.push("  ops <unknown>".to_string());
                    } else if oc == 0 {
                        outputs.push("  ops <none>".to_string());
                    } else {
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
            }
            Stmt::Call(tname, opname, args) => {
                unsafe {
                    let (tn_str, mut self_obj, should_delete_self) = if let Some(Value::Object(obj)) = self.env.get(tname) {
                        (obj.type_name.clone(), zig_ffi::typed_obj { ptr: obj.obj.ptr, r#type: obj.obj.r#type }, false)
                    } else {
                        (tname.clone(), zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() }, true)
                    };

                    let tn = std::ffi::CString::new(tn_str.as_str()).unwrap();
                    let on = std::ffi::CString::new(opname.as_str()).unwrap();
                    let mut sig_ptr: *const zig_ffi::dyn_method_sig = std::ptr::null();
                    let _ = zig_ffi::dyn_oop_get_op_typed(tn.as_ptr(), on.as_ptr(), &mut sig_ptr);
                    
                    let mut out_obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                    let mut arg_objs: Vec<zig_ffi::typed_obj> = Vec::new();
                    for a in args {
                        let v = eval(a, &self.env, &self.ops)?;
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
                            _ => {
                                return Err("unsupported arg type".to_string());
                            }
                        }
                    }
                    let rc = zig_ffi::dyn_oop_do_op(tn.as_ptr(), on.as_ptr(), &mut self_obj, if arg_objs.is_empty() { std::ptr::null() } else { arg_objs.as_ptr() }, arg_objs.len() as zig_ffi::CInt, &mut out_obj);
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
                    if should_delete_self {
                        let _ = zig_ffi::typed_obj_delete(&mut self_obj);
                    }
                }
            }
            Stmt::RunList(items) => {
                if items.is_empty() {
                    outputs.push("empty list".to_string());
                } else {
                    match &items[0] {
                        Expr::Var(fname) => {
                            let mut argsv = Vec::new();
                            for e in items.iter().skip(1) {
                                argsv.push(eval(e, &self.env, &self.ops)?);
                            }
                            let out = self.call_func(fname, &argsv)?;
                            outputs.push(to_string(&out));
                        }
                        _ => return Err("run expects list with function name first".to_string()),
                    }
                }
            }
            Stmt::Return(_) => {
                return Err("return outside function".to_string());
            }
            Stmt::ExprStmt(expr) => {
                let _ = eval(expr, &self.env, &self.ops)?;
            }
        }
    }
        Ok(outputs)
    }
    fn call_func(&self, name: &str, args: &[Value]) -> Result<Value, String> {
        match self.env.get(name) {
            Some(func_val) => apply_func(func_val, args, &self.ops, &self.env),
            None => Err(format!("undefined function: {}", name)),
        }
    }
}

        mod zig_ffi {
            use libloading::{Library, Symbol};
            use std::sync::OnceLock;
            pub type CInt = i32;
            static LIB: OnceLock<Library> = OnceLock::new();
            fn lib() -> &'static Library {
                LIB.get_or_init(|| unsafe { Library::new("dyn_sys_zig_dll.dll") }.expect("load dyn_sys_zig_dll.dll"))
            }
            pub type DynCallFn = unsafe extern "C" fn(
                self_obj: *mut typed_obj,
                args: *const typed_obj,
                argc: CInt,
                ret_out: *mut typed_obj,
            ) -> CInt;
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
                pub param_count: CInt,
                pub desc: *const std::os::raw::c_char,
            }
            pub unsafe fn dyn_oop_get_type_count() -> CInt {
                let f: Symbol<unsafe extern "C" fn() -> CInt> = lib().get(b"dyn_oop_get_type_count\0").unwrap();
                f()
            }
            pub unsafe fn dyn_oop_get_type_name(index: CInt) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(CInt) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_type_name\0").unwrap();
                f(index)
            }
            pub unsafe fn dyn_oop_get_type_desc(type_name: *const std::os::raw::c_char) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_type_desc\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_type_kind(type_name: *const std::os::raw::c_char) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> CInt> = lib().get(b"dyn_oop_get_type_kind\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_type_size(type_name: *const std::os::raw::c_char) -> usize {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> usize> = lib().get(b"dyn_oop_get_type_size\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_field_count(type_name: *const std::os::raw::c_char) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> CInt> = lib().get(b"dyn_oop_get_field_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_field_name(type_name: *const std::os::raw::c_char, index: CInt) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, CInt) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_field_name\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_method_count(type_name: *const std::os::raw::c_char) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> CInt> = lib().get(b"dyn_oop_get_method_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_method_name(type_name: *const std::os::raw::c_char, index: CInt) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, CInt) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_method_name\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_method_sig_str(type_name: *const std::os::raw::c_char, index: CInt) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, CInt) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_method_sig_str\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_op_count(type_name: *const std::os::raw::c_char) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> CInt> = lib().get(b"dyn_oop_get_op_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_op_meta(
                type_name: *const std::os::raw::c_char,
                index: CInt,
                name_out: *mut *const std::os::raw::c_char,
                sig_out: *mut *const std::os::raw::c_char,
                desc_out: *mut *const std::os::raw::c_char,
            ) -> CInt {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        CInt,
                        *mut *const std::os::raw::c_char,
                        *mut *const std::os::raw::c_char,
                        *mut *const std::os::raw::c_char,
                    ) -> CInt,
                > = lib().get(b"dyn_oop_get_op_meta\0").unwrap();
                f(type_name, index, name_out, sig_out, desc_out)
            }
            pub unsafe fn dyn_oop_get_op_typed(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                sig_out: *mut *const dyn_method_sig,
            ) -> CInt {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *mut *const dyn_method_sig,
                    ) -> CInt,
                > = lib().get(b"dyn_oop_get_op_typed\0").unwrap();
                f(type_name, op_name, sig_out)
            }
            pub unsafe fn dyn_oop_get_type_def(type_name: *const std::os::raw::c_char) -> *const std::ffi::c_void {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> *const std::ffi::c_void> = lib().get(b"dyn_oop_get_type_def\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_register_type(type_name: *const std::os::raw::c_char) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> CInt> = lib().get(b"dyn_oop_register_type\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_set_type_meta(
                type_name: *const std::os::raw::c_char,
                desc: *const std::os::raw::c_char,
                def: *const std::ffi::c_void,
            ) -> CInt {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *const std::ffi::c_void,
                    ) -> CInt,
                > = lib().get(b"dyn_oop_set_type_meta\0").unwrap();
                f(type_name, desc, def)
            }
            pub unsafe fn dyn_oop_register_op_typed(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                sig: *const dyn_method_sig,
                op_desc: *const std::os::raw::c_char,
                fn_ptr: DynCallFn,
            ) -> CInt {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *const dyn_method_sig,
                        *const std::os::raw::c_char,
                        DynCallFn,
                    ) -> CInt,
                > = lib().get(b"dyn_oop_register_op_typed\0").unwrap();
                f(type_name, op_name, sig, op_desc, fn_ptr)
            }
            pub unsafe fn get_i64_def() -> *const std::ffi::c_void {
                let s: Symbol<*const std::ffi::c_void> = lib().get(b"i64_def\0").unwrap();
                *s
            }
            pub unsafe fn typed_obj_alloc(o: *mut typed_obj, def: *const std::ffi::c_void) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*mut typed_obj, *const std::ffi::c_void) -> CInt> = lib().get(b"typed_obj_alloc\0").unwrap();
                f(o, def)
            }
            pub unsafe fn typed_obj_delete(o: *mut typed_obj) -> CInt {
                let f: Symbol<unsafe extern "C" fn(*mut typed_obj) -> CInt> = lib().get(b"typed_obj_delete\0").unwrap();
                f(o)
            }
            pub unsafe fn dyn_oop_do_op(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                self_obj: *mut typed_obj,
                args: *const typed_obj,
                argc: CInt,
                out: *mut typed_obj,
            ) -> CInt {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *mut typed_obj,
                        *const typed_obj,
                        CInt,
                        *mut typed_obj,
                    ) -> CInt,
                > = lib().get(b"dyn_oop_do_op\0").unwrap();
                f(type_name, op_name, self_obj, args, argc, out)
            }
        }

fn init_oop_demo_once() {
    use std::sync::Once;
    static INIT: Once = Once::new();
    INIT.call_once(|| unsafe {
        let i64_name = std::ffi::CString::new("i64").unwrap();
        let _ = zig_ffi::dyn_oop_register_type(i64_name.as_ptr());
        let i64_def = zig_ffi::get_i64_def();
        let _ = zig_ffi::dyn_oop_set_type_meta(i64_name.as_ptr(), std::ptr::null(), i64_def);

        let tname = std::ffi::CString::new("testobj").unwrap();
        let desc = std::ffi::CString::new("demo type").unwrap();
        let _ = zig_ffi::dyn_oop_register_type(tname.as_ptr());
        let _ = zig_ffi::dyn_oop_set_type_meta(tname.as_ptr(), desc.as_ptr(), i64_def);
        let tname2 = std::ffi::CString::new("test_struct").unwrap();
        let desc2 = std::ffi::CString::new("demo struct").unwrap();
        let _ = zig_ffi::dyn_oop_register_type(tname2.as_ptr());
        let _ = zig_ffi::dyn_oop_set_type_meta(tname2.as_ptr(), desc2.as_ptr(), i64_def);
        static I64: &[u8] = b"i64\0";
        static ADD: &[u8] = b"add\0";
        static ADD_DESC: &[u8] = b"sum two i64\0";
        let p0 = zig_ffi::dyn_param_desc {
            name: std::ptr::null(),
            type_name: I64.as_ptr() as *const std::os::raw::c_char,
            desc: std::ptr::null(),
        };
        let p1 = zig_ffi::dyn_param_desc {
            name: std::ptr::null(),
            type_name: I64.as_ptr() as *const std::os::raw::c_char,
            desc: std::ptr::null(),
        };
        let params_box = vec![p0, p1].into_boxed_slice();
        let params_ptr = params_box.as_ptr();
        std::mem::forget(params_box);
        let sig = zig_ffi::dyn_method_sig {
            ret_type_name: I64.as_ptr() as *const std::os::raw::c_char,
            params: params_ptr,
            param_count: 2,
            desc: std::ptr::null(),
        };
        let _ = zig_ffi::dyn_oop_register_op_typed(
            tname.as_ptr(),
            ADD.as_ptr() as *const std::os::raw::c_char,
            &sig,
            ADD_DESC.as_ptr() as *const std::os::raw::c_char,
            demo_add_cb,
        );
    });
}

pub fn ensure_oop_demo_registered() {
    init_oop_demo_once();
}
unsafe extern "C" fn demo_add_cb(
    _self_obj: *mut zig_ffi::typed_obj,
    args: *const zig_ffi::typed_obj,
    argc: zig_ffi::CInt,
    ret_out: *mut zig_ffi::typed_obj,
) -> zig_ffi::CInt {
    if args.is_null() || ret_out.is_null() || argc < 2 {
        return -1;
    }
    let args_slice = std::slice::from_raw_parts(args, argc as usize);
    let a = *(args_slice[0].ptr as *const i64);
    let b = *(args_slice[1].ptr as *const i64);
    let def = zig_ffi::get_i64_def();
    if zig_ffi::typed_obj_alloc(ret_out, def) != 0 {
        return -1;
    }
    *(ret_out.as_ref().unwrap().ptr as *mut i64) = a + b;
    0
}

pub fn run(program: &[Stmt]) -> Result<Vec<String>, String> {
    let mut it = Interpreter::new();
    it.run(program)
}

fn to_string(v: &Value) -> String {
    match v {
        Value::Int(i) => i.to_string(),
        Value::Str(s) => s.clone(),
        Value::Sym(s) => format!("@{}", s), // Representation of symbol
        Value::List(xs, _) => {
            let mut out = String::new();
            out.push('(');
            for (i, v) in xs.iter().enumerate() {
                if i > 0 { out.push(' '); }
                out.push_str(&to_string(v));
            }
            out.push(')');
            out
        }
        Value::Func(_, _, _) => "<func>".to_string(),
        Value::FuncBlock(_, _, _) => "<func>".to_string(),
        Value::NativeFunc(_) => "<native func>".to_string(),
        Value::Object(_) => "<object>".to_string(),
    }
}

fn call_oop_method(obj_val: &Value, op_name: &str, args: &[Value]) -> Result<Value, String> {
    unsafe {
        let (tn_str, mut self_obj, should_delete_self) = match obj_val {
            Value::Object(obj) => (obj.type_name.clone(), zig_ffi::typed_obj { ptr: obj.obj.ptr, r#type: obj.obj.r#type }, false),
            Value::Sym(s) => (s.clone(), zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() }, true),
            _ => return Err("method call requires object or type symbol".to_string()),
        };

        let tn = std::ffi::CString::new(tn_str.as_str()).unwrap();
        let on = std::ffi::CString::new(op_name).unwrap();
        let mut sig_ptr: *const zig_ffi::dyn_method_sig = std::ptr::null();
        let _ = zig_ffi::dyn_oop_get_op_typed(tn.as_ptr(), on.as_ptr(), &mut sig_ptr);
        
        let mut out_obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
        let mut arg_objs: Vec<zig_ffi::typed_obj> = Vec::new();
        for a in args {
            match a {
                Value::Int(i) => {
                    let td = zig_ffi::dyn_oop_get_type_def(std::ffi::CString::new("i64").unwrap().as_ptr());
                    let mut to = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
                    if zig_ffi::typed_obj_alloc(&mut to, td) == 0 && !to.ptr.is_null() {
                        *(to.ptr as *mut i64) = *i;
                        arg_objs.push(to);
                    } else {
                        return Err("alloc arg failed".to_string());
                    }
                }
                Value::Str(_) => return Err("string args not supported".to_string()),
                _ => return Err("unsupported arg type".to_string()),
            }
        }
        let rc = zig_ffi::dyn_oop_do_op(tn.as_ptr(), on.as_ptr(), &mut self_obj, if arg_objs.is_empty() { std::ptr::null() } else { arg_objs.as_ptr() }, arg_objs.len() as zig_ffi::CInt, &mut out_obj);
        
        let res = if rc < 0 {
            Err("op call failed".to_string())
        } else {
            if !out_obj.ptr.is_null() && !out_obj.r#type.is_null() && !sig_ptr.is_null() {
                let retn = (*sig_ptr).ret_type_name;
                if !retn.is_null() {
                    let ret_type_str = std::ffi::CStr::from_ptr(retn).to_string_lossy().into_owned();
                    if ret_type_str == "i64" {
                        let val = *(out_obj.ptr as *const i64);
                        let _ = zig_ffi::typed_obj_delete(&mut out_obj);
                        Ok(Value::Int(val))
                    } else {
                        Ok(Value::Object(Rc::new(OopObject { obj: out_obj, type_name: ret_type_str })))
                    }
                } else {
                    let _ = zig_ffi::typed_obj_delete(&mut out_obj);
                    Ok(Value::Int(rc as i64))
                }
            } else {
                let _ = zig_ffi::typed_obj_delete(&mut out_obj);
                Ok(Value::Int(rc as i64))
            }
        };

        for mut o in arg_objs {
            let _ = zig_ffi::typed_obj_delete(&mut o);
        }
        if should_delete_self {
            let _ = zig_ffi::typed_obj_delete(&mut self_obj);
        }
        res
    }
}

fn eval(expr: &Expr, env: &HashMap<String, Value>, ops: &HashMap<String, String>) -> Result<Value, String> {
    match expr {
        Expr::Int(i) => Ok(Value::Int(*i)),
        Expr::Str(s) => Ok(Value::Str(s.clone())),
        Expr::Sym(s) => Ok(Value::Sym(s.clone())),
        Expr::SymToStr(e) => {
            let v = eval(e, env, ops)?;
            match v {
                Value::Sym(s) => Ok(Value::Str(s)),
                _ => Err("expected symbol".to_string()),
            }
        }
        Expr::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| format!("undefined variable: {}", name)),
        Expr::List(items) => {
            // Check if it's a function call (Lisp-style)
            // Only if:
            // 1. Not empty
            // 2. First item has no key
            // 3. First item evaluates to a function (or is a Var/Sym that resolves to one)
            // 4. AND it's not a special form symbol like "let" (though "let" usually isn't in env as func)
            
            let treat_as_call = if !items.is_empty() && items[0].key.is_none() {
                // Try to resolve head
                match &items[0].value {
                     Expr::Var(s) | Expr::Sym(s) => {
                         if let Some(v) = env.get(s) {
                             matches!(v, Value::Func(..) | Value::FuncBlock(..) | Value::NativeFunc(..))
                         } else {
                             false
                         }
                     }
                     // If head is a list ((f) ...), try to eval it?
                     Expr::List(_) => {
                         if let Ok(v) = eval(&items[0].value, env, ops) {
                              matches!(v, Value::Func(..) | Value::FuncBlock(..) | Value::NativeFunc(..))
                         } else {
                             false
                         }
                     }
                     _ => false
                }
            } else {
                false
            };

            if treat_as_call {
                // Evaluate head
                let func = eval(&items[0].value, env, ops)?;
                // Evaluate args
                let mut args = Vec::new();
                for item in items.iter().skip(1) {
                    if item.key.is_some() {
                         return Err("named arguments not supported in Lisp-style call".to_string());
                    }
                    args.push(eval(&item.value, env, ops)?);
                }
                // Call
                apply_func(&func, &args, ops, env)
            } else {
                // Return as List Value
                let mut vals = Vec::new();
                let mut keys = HashMap::new();
                for (i, item) in items.iter().enumerate() {
                    vals.push(eval(&item.value, env, ops)?);
                    if let Some(k) = &item.key {
                        keys.insert(k.clone(), i);
                    }
                }
                Ok(Value::List(vals, keys))
            }
        }
        Expr::Call(fname, args) => {
            let mut argv = Vec::new();
            for a in args {
                argv.push(eval(a, env, ops)?);
            }
            if let Some(func_val) = env.get(name) {
                apply_func(func_val, &argv, ops, env)
            } else {
                Err(format!("undefined function: {}", name))
            }
        }
        Expr::MethodCall(obj_expr, op_name, args) => {
            let obj_val = eval(obj_expr, env, ops)?;
            let mut argv = Vec::new();
            for a in args {
                argv.push(eval(a, env, ops)?);
            }
            call_oop_method(&obj_val, op_name, &argv)
        }
        Expr::Add(a, b) => bin_int(a, b, env, ops, |x, y| x + y),
        Expr::Sub(a, b) => bin_int(a, b, env, ops, |x, y| x - y),
        Expr::Mul(a, b) => bin_int(a, b, env, ops, |x, y| x * y),
        Expr::Div(a, b) => bin_int(a, b, env, ops, |x, y| x / y),
        Expr::Mod(a, b) => bin_int(a, b, env, ops, |x, y| x % y),
        Expr::Pow(a, b) => {
            let va = eval(a, env, ops)?;
            let vb = eval(b, env, ops)?;
            match (va, vb) {
                (Value::Int(x), Value::Int(y)) => {
                    if y < 0 { Err("negative exponent not supported".to_string()) }
                    else { Ok(Value::Int(x.pow(y as u32))) }
                }
                _ => Err("type error: expected integers".to_string()),
            }
        }
        Expr::Custom(a, op, b) => {
            let va = eval(a, env, ops)?;
            let vb = eval(b, env, ops)?;
            let fname = ops.get(op).ok_or_else(|| format!("undefined custom operator: {}", op))?;
            if let Some(func_val) = env.get(fname) {
                apply_func(func_val, &[va, vb], ops, env)
            } else {
                Err(format!("undefined operator function: {}", fname))
            }
        }
        Expr::Group(e) => eval(e, env, ops),
        Expr::Map(list_expr, func_name) => {
            let lv = eval(list_expr, env, ops)?;
            match lv {
                Value::List(xs, _) => {
                    if let Some(func_val) = env.get(func_name) {
                        let mut out = Vec::new();
                        for x in xs {
                            out.push(apply_func(func_val, &[x], ops, env)?);
                        }
                        Ok(Value::List(out, HashMap::new()))
                    } else {
                        Err(format!("undefined function: {}", func_name))
                    }
                }
                _ => Err("map left operand must be a list".to_string()),
            }
        }
        Expr::Index(list_expr, idx_expr) => {
            let lv = eval(list_expr, env, ops)?;
            match lv {
                Value::List(xs, keys) => {
                    let iv = eval(idx_expr, env, ops)?;
                    match iv {
                        Value::Int(i) => {
                            let idx = i as usize;
                            xs.get(idx).cloned().ok_or_else(|| "index out of range".to_string())
                        }
                        Value::Str(s) => {
                            if let Some(&idx) = keys.get(&s) {
                                xs.get(idx).cloned().ok_or_else(|| "index out of range".to_string())
                            } else {
                                Err("key not found".to_string())
                            }
                        }
                        _ => Err("index must be int or string".to_string()),
                    }
                }
                _ => Err("indexing requires a list".to_string()),
            }
        }
    }
}

fn eval_block(stmts: &[Stmt], env: &mut HashMap<String, Value>, ops: &HashMap<String, String>) -> Result<Value, String> {
    for stmt in stmts {
        match stmt {
            Stmt::Let(name, expr) => {
                let v = eval(expr, env, ops)?;
                env.insert(name.clone(), v);
            }
            Stmt::LetFunc(name, params, body) => {
                let func_env = env.clone();
                env.insert(name.clone(), Value::Func(params.clone(), (*body).clone(), func_env));
            }
            Stmt::LetFuncBlock(name, params, body) => {
                let func_env = env.clone();
                env.insert(name.clone(), Value::FuncBlock(params.clone(), body.clone(), func_env));
            }
            Stmt::ExprStmt(e) => {
                let _ = eval(e, env, ops)?;
            }
            Stmt::PrintExpr(e) => {
                 let v = eval(e, env, ops)?;
                 println!("{}", to_string(&v));
            }
            Stmt::PrintTemplate(tpl, arg) => {
                if let Some(arg) = arg {
                    let v = eval(arg, env, ops)?;
                    let vstr = to_string(&v);
                    println!("{}", tpl.replace("{}", &vstr));
                } else {
                    println!("{}", tpl);
                }
            }
            Stmt::Return(e) => {
                return eval(e, env, ops);
            }
            Stmt::ListTypes | Stmt::ListType(_) | Stmt::RegOp(_, _) | Stmt::Call(_, _, _) | Stmt::RunList(_) => {
                // For now, these might not be fully supported inside blocks if they rely on Interpreter state (outputs)
                // But we can support basic ones.
                // Wait, Stmt::Call etc are handled in Interpreter::run which returns outputs.
                // eval_block is "silent" unless it prints?
                // The Interpreter::run accumulates outputs.
                // eval_block currently doesn't have access to "outputs" vec.
                // So Print statements inside function block will print to stdout directly?
                // The previous implementation ignored Print statements (empty block).
                // I will make them print to stdout for now, or just ignore them if that's the desired behavior.
                // But generally functions should be able to print.
                // However, the signature of eval_block doesn't allow returning output strings.
                // I'll stick to println! for now for debugging, or maybe I should change signature to return outputs too?
                // No, simpler is better.
                // But Stmt::Call relies on Interpreter::run logic?
                // Stmt::Call in Interpreter::run executes FFI calls.
                // I should probably move the logic of Stmt execution to a common place if I want full support.
                // For now, I'll just ignore unsupported statements or return error.
                // Stmt::Call is actually not an Expr, it's a Stmt.
                // If it's Expr::Call, it's handled in eval.
                // Stmt::Call is `call t.op(args)`.
                return Err("unsupported statement in function block".to_string());
            }
        }
    }
    Ok(Value::Int(0))
}
fn bin_int(
    a: &Expr,
    b: &Expr,
    env: &HashMap<String, Value>,
    ops: &HashMap<String, String>,
    f: impl Fn(i64, i64) -> i64,
) -> Result<Value, String> {
    let va = eval(a, env, ops)?;
    let vb = eval(b, env, ops)?;
    match (va, vb) {
        (Value::Int(x), Value::Int(y)) => Ok(Value::Int(f(x, y))),
        _ => Err("type error: expected integers".to_string()),
    }
}
