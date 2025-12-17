use std::collections::HashMap;
use std::ffi::CStr;
use crate::ast::{Stmt, Expr, ListItem};

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Str(String),
    List(Vec<Value>, std::collections::HashMap<String, usize>),
    Func(Vec<String>, Vec<Stmt>, HashMap<String, Value>),
}

type BlockHandler = fn(&mut Interpreter, Vec<ListItem>, Vec<Stmt>) -> Result<Vec<String>, String>;

pub struct Interpreter {
    env: HashMap<String, Value>,
    ops: HashMap<String, String>,
    test_mode: bool,
    block_handlers: HashMap<String, BlockHandler>,
}

impl Interpreter {
    pub fn new(test_mode: bool) -> Self {
        init_oop_demo_once();
        let mut interpreter = Interpreter {
            env: HashMap::new(),
            ops: HashMap::new(),
            test_mode,
            block_handlers: HashMap::new(),
        };
        interpreter.register_block_handler("test", Self::handle_test);
        interpreter.register_block_handler("test_init", Self::handle_test_init);
        interpreter.register_block_handler("notest", Self::handle_notest);
        interpreter
    }

    pub fn register_block_handler(&mut self, name: &str, handler: BlockHandler) {
        self.block_handlers.insert(name.to_string(), handler);
    }

    fn handle_test(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<Vec<String>, String> {
        if !interp.test_mode { return Ok(vec![]); }
        let saved_env = interp.env.clone();
        let saved_ops = interp.ops.clone();
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let block_outputs = interp.run(&body)?;
        interp.test_mode = saved_mode;
        interp.env = saved_env;
        interp.ops = saved_ops;
        Ok(block_outputs)
    }

    fn handle_test_init(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<Vec<String>, String> {
        if !interp.test_mode { return Ok(vec![]); }
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let block_outputs = interp.run(&body)?;
        interp.test_mode = saved_mode;
        Ok(block_outputs)
    }

    fn handle_notest(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<Vec<String>, String> {
        if interp.test_mode { return Ok(vec![]); }
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let block_outputs = interp.run(&body)?;
        interp.test_mode = saved_mode;
        Ok(block_outputs)
    }

    pub fn run(&mut self, program: &[Stmt]) -> Result<Vec<String>, String> {
        let mut outputs = Vec::new();
        for stmt in program {
            match stmt {
                Stmt::Let(name, expr) => {
                    let v = eval(expr, &self.env, &self.ops)?;
                    self.env.insert(name.clone(), v);
                }
                Stmt::LetFunc(name, params, body) => {
                    let func_env = self.env.clone();
                    let v = Value::Func(params.clone(), body.clone(), func_env);
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
                    let tn = std::ffi::CString::new(tname.as_str()).unwrap();
                    let on = std::ffi::CString::new(opname.as_str()).unwrap();
                    let mut sig_ptr: *const zig_ffi::dyn_method_sig = std::ptr::null();
                    let _ = zig_ffi::dyn_oop_get_op_typed(tn.as_ptr(), on.as_ptr(), &mut sig_ptr);
                    let mut self_obj = zig_ffi::typed_obj { ptr: std::ptr::null_mut(), r#type: std::ptr::null() };
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
            }
            Stmt::RunList(items) => {
                if items.is_empty() {
                    outputs.push("empty list".to_string());
                } else {
                    match &items[0] {
                        Expr::Sym(name) => {
                            if let Some(Value::Func(_, _, _)) = self.env.get(name) {
                                let mut argsv = Vec::new();
                                for e in items.iter().skip(1) {
                                    argsv.push(eval(e, &self.env, &self.ops)?);
                                }
                                let out = self.call_func(name, &argsv)?;
                                outputs.push(to_string(&out));
                            } else {
                                let stmt = list_to_stmt(items)?;
                                let inner = self.run(std::slice::from_ref(&stmt))?;
                                outputs.extend(inner);
                            }
                        }
                        _ => {
                            let stmt = list_to_stmt(items)?;
                            let inner = self.run(std::slice::from_ref(&stmt))?;
                            outputs.extend(inner);
                        }
                    }
                }
            }
            Stmt::Return(opt_val) => {
                if let Some(val) = opt_val {
                    let v = eval(val, &self.env, &self.ops)?;
                    // Since eval returns Value, we can't directly return it from run which returns Strings.
                    // But Return is usually inside a function call.
                    // If we are at top level, it's an error.
                    // Wait, `run` returns `Result<Vec<String>, String>`.
                    // We need to signal a Return. 
                    // But currently `run` doesn't support returning Value directly to caller of `run`.
                    // It only supports printing.
                    // Actually, `call_func` uses `eval_block` which is what we need.
                    // But `eval_block` logic is inside `run`.
                    // We need to refactor `run` to support return values or just error here.
                    // For now, let's just error if return is hit in `run` because `run` expects to output strings.
                    // The function execution logic in `call_func` needs to handle Return.
                    // Let's look at `call_func`.
                    // `call_func` calls `self.run(&body)`.
                    // If `run` encounters `Return`, it should return that value.
                    // But `run` returns `Vec<String>`.
                    // This implies `Return` statement inside `run` is problematic if `run` signature is fixed.
                    // We need `run` to return `Result<(Vec<String>, Option<Value>), String>`.
                    // Or `call_func` should handle execution differently.
                    // Let's check `call_func`.
                    
                    // Actually, `call_func` implementation:
                    // `let outputs = self.run(&body)?;`
                    // `if let Some(last) = outputs.last() { ... }`
                    // This suggests function return value is printed? No, that's not right.
                    // Previous implementation of `LetFunc` body was `Expr`.
                    // Now it is `Vec<Stmt>`.
                    // If `Return` is used, we need to propagate the value.
                    // But `run` returns strings.
                    // This is a design issue in `run`.
                    // For now, to fix compilation, I will implement `Return` as error in `run` or just ignore it?
                    // The error `Return(Expr)` vs `Return(Option<Expr>)` was fixed in AST.
                    // Now we need to handle `Option<Expr>` here.
                    
                    return Err("Return statement not supported in top-level run".to_string());
                } else {
                     return Err("Return statement not supported in top-level run".to_string());
                }
            }
            Stmt::ExprStmt(expr) => {
                let _ = eval(expr, &self.env, &self.ops)?;
            }
            Stmt::Block(name, meta, stmts) => {
                if let Some(&handler) = self.block_handlers.get(name) {
                    outputs.extend(handler(self, meta.clone(), stmts.clone())?);
                } else {
                    return Err(format!("Unknown block type: {}", name));
                }
            }
        }
    }
        Ok(outputs)
    }
    fn call_func(&self, name: &str, args: &[Value]) -> Result<Value, String> {
        match self.env.get(name) {
            Some(Value::Func(params, body_stmts, fenv)) => {
                let mut call_env = fenv.clone();
                if args.len() != params.len() {
                    return Err(format!("arity mismatch: expected {}, got {}", params.len(), args.len()));
                }
                for (p, v) in params.iter().zip(args.iter()) {
                    call_env.insert(p.clone(), v.clone());
                }
                eval_func_body(&body_stmts, &mut call_env, &self.ops)
            }
            _ => Err(format!("undefined function: {}", name)),
        }
    }
}

        mod zig_ffi {
            use libloading::{Library, Symbol};
            use std::sync::OnceLock;
            pub type c_int = i32;
            static LIB: OnceLock<Library> = OnceLock::new();
            fn lib() -> &'static Library {
                LIB.get_or_init(|| unsafe { Library::new("dyn_sys_zig_dll.dll") }.expect("load dyn_sys_zig_dll.dll"))
            }
            pub type DynCallFn = unsafe extern "C" fn(
                self_obj: *mut typed_obj,
                args: *const typed_obj,
                argc: c_int,
                ret_out: *mut typed_obj,
            ) -> c_int;
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
            pub unsafe fn dyn_oop_get_type_count() -> c_int {
                let f: Symbol<unsafe extern "C" fn() -> c_int> = lib().get(b"dyn_oop_get_type_count\0").unwrap();
                f()
            }
            pub unsafe fn dyn_oop_get_type_name(index: c_int) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(c_int) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_type_name\0").unwrap();
                f(index)
            }
            pub unsafe fn dyn_oop_get_type_desc(type_name: *const std::os::raw::c_char) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_type_desc\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_type_kind(type_name: *const std::os::raw::c_char) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> c_int> = lib().get(b"dyn_oop_get_type_kind\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_type_size(type_name: *const std::os::raw::c_char) -> usize {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> usize> = lib().get(b"dyn_oop_get_type_size\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_field_count(type_name: *const std::os::raw::c_char) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> c_int> = lib().get(b"dyn_oop_get_field_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_field_name(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, c_int) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_field_name\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_method_count(type_name: *const std::os::raw::c_char) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> c_int> = lib().get(b"dyn_oop_get_method_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_method_name(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, c_int) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_method_name\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_method_sig_str(type_name: *const std::os::raw::c_char, index: c_int) -> *const std::os::raw::c_char {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char, c_int) -> *const std::os::raw::c_char> = lib().get(b"dyn_oop_get_method_sig_str\0").unwrap();
                f(type_name, index)
            }
            pub unsafe fn dyn_oop_get_op_count(type_name: *const std::os::raw::c_char) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> c_int> = lib().get(b"dyn_oop_get_op_count\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_get_op_meta(
                type_name: *const std::os::raw::c_char,
                index: c_int,
                name_out: *mut *const std::os::raw::c_char,
                sig_out: *mut *const std::os::raw::c_char,
                desc_out: *mut *const std::os::raw::c_char,
            ) -> c_int {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        c_int,
                        *mut *const std::os::raw::c_char,
                        *mut *const std::os::raw::c_char,
                        *mut *const std::os::raw::c_char,
                    ) -> c_int,
                > = lib().get(b"dyn_oop_get_op_meta\0").unwrap();
                f(type_name, index, name_out, sig_out, desc_out)
            }
            pub unsafe fn dyn_oop_get_op_typed(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                sig_out: *mut *const dyn_method_sig,
            ) -> c_int {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *mut *const dyn_method_sig,
                    ) -> c_int,
                > = lib().get(b"dyn_oop_get_op_typed\0").unwrap();
                f(type_name, op_name, sig_out)
            }
            pub unsafe fn dyn_oop_get_type_def(type_name: *const std::os::raw::c_char) -> *const std::ffi::c_void {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> *const std::ffi::c_void> = lib().get(b"dyn_oop_get_type_def\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_register_type(type_name: *const std::os::raw::c_char) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*const std::os::raw::c_char) -> c_int> = lib().get(b"dyn_oop_register_type\0").unwrap();
                f(type_name)
            }
            pub unsafe fn dyn_oop_set_type_meta(
                type_name: *const std::os::raw::c_char,
                desc: *const std::os::raw::c_char,
                def: *const std::ffi::c_void,
            ) -> c_int {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *const std::ffi::c_void,
                    ) -> c_int,
                > = lib().get(b"dyn_oop_set_type_meta\0").unwrap();
                f(type_name, desc, def)
            }
            pub unsafe fn dyn_oop_register_op_typed(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                sig: *const dyn_method_sig,
                op_desc: *const std::os::raw::c_char,
                fn_ptr: DynCallFn,
            ) -> c_int {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *const dyn_method_sig,
                        *const std::os::raw::c_char,
                        DynCallFn,
                    ) -> c_int,
                > = lib().get(b"dyn_oop_register_op_typed\0").unwrap();
                f(type_name, op_name, sig, op_desc, fn_ptr)
            }
            pub unsafe fn get_i64_def() -> *const std::ffi::c_void {
                let s: Symbol<*const std::ffi::c_void> = lib().get(b"i64_def\0").unwrap();
                *s
            }
            pub unsafe fn typed_obj_alloc(o: *mut typed_obj, def: *const std::ffi::c_void) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*mut typed_obj, *const std::ffi::c_void) -> c_int> = lib().get(b"typed_obj_alloc\0").unwrap();
                f(o, def)
            }
            pub unsafe fn typed_obj_delete(o: *mut typed_obj) -> c_int {
                let f: Symbol<unsafe extern "C" fn(*mut typed_obj) -> c_int> = lib().get(b"typed_obj_delete\0").unwrap();
                f(o)
            }
            pub unsafe fn dyn_oop_do_op(
                type_name: *const std::os::raw::c_char,
                op_name: *const std::os::raw::c_char,
                self_obj: *mut typed_obj,
                args: *const typed_obj,
                argc: c_int,
                out: *mut typed_obj,
            ) -> c_int {
                let f: Symbol<
                    unsafe extern "C" fn(
                        *const std::os::raw::c_char,
                        *const std::os::raw::c_char,
                        *mut typed_obj,
                        *const typed_obj,
                        c_int,
                        *mut typed_obj,
                    ) -> c_int,
                > = lib().get(b"dyn_oop_do_op\0").unwrap();
                f(type_name, op_name, self_obj, args, argc, out)
            }
        }

fn init_oop_demo_once() {
    use std::sync::Once;
    static INIT: Once = Once::new();
    INIT.call_once(|| unsafe {
        let tname = std::ffi::CString::new("testobj").unwrap();
        let desc = std::ffi::CString::new("demo type").unwrap();
        let _ = zig_ffi::dyn_oop_register_type(tname.as_ptr());
        let i64_def = zig_ffi::get_i64_def();
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
    argc: zig_ffi::c_int,
    ret_out: *mut zig_ffi::typed_obj,
) -> zig_ffi::c_int {
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
    let mut it = Interpreter::new(false);
    it.run(program)
}

pub fn run_tests(program: &[Stmt]) -> Result<Vec<String>, String> {
    let mut it = Interpreter::new(true);
    it.run(program)
}

fn to_string(v: &Value) -> String {
    match v {
        Value::Int(i) => i.to_string(),
        Value::Str(s) => s.clone(),
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
    }
}

fn eval(expr: &Expr, env: &HashMap<String, Value>, ops: &HashMap<String, String>) -> Result<Value, String> {
    match expr {
        Expr::Int(i) => Ok(Value::Int(*i)),
        Expr::Str(s) => Ok(Value::Str(s.clone())),
        Expr::Sym(s) => Ok(Value::Str(s.clone())),
        Expr::Var(name) => env
            .get(name)
            .cloned()
            .ok_or_else(|| format!("undefined variable: {}", name)),
        Expr::SList(items) => {
            let mut vals = Vec::new();
            for it in items {
                vals.push(eval(it, env, ops)?);
            }
            Ok(Value::List(vals, HashMap::new()))
        }
        Expr::List(items) => {
            let mut vals = Vec::new();
            let mut keys = HashMap::new();
            for (idx, it) in items.iter().enumerate() {
                // Treat Var as Sym in list construction (Symbol Table behavior)
                let v = match &it.value {
                    Expr::Var(name) => Value::Str(name.clone()),
                    _ => eval(&it.value, env, ops)?,
                };
                vals.push(v);
                if let Some(k) = &it.key {
                    keys.insert(k.clone(), idx);
                }
            }
            Ok(Value::List(vals, keys))
        }
        Expr::Call(name, args) => {
            if name == "value_of" {
                if args.len() != 1 {
                    return Err("value_of expects 1 argument".to_string());
                }
                let list_val = eval(&args[0], env, ops)?;
                if let Value::List(items, _keys) = list_val {
                    let mut evaluated_items = Vec::new();
                    for item in items {
                         match item {
                             Value::Str(s) => {
                                 if let Some(val) = env.get(&s) {
                                     evaluated_items.push(val.clone());
                                 } else {
                                     return Err(format!("undefined variable: {}", s));
                                 }
                             }
                             _ => evaluated_items.push(item),
                         }
                    }
                    return Ok(Value::List(evaluated_items, HashMap::new()));
                } else {
                    return Err("value_of expects a list".to_string());
                }
            }
            let mut argv = Vec::new();
            for a in args {
                argv.push(eval(a, env, ops)?);
            }
            if let Some(Value::Func(params, body, fenv)) = env.get(name) {
                let mut call_env = fenv.clone();
                if argv.len() != params.len() {
                    return Err(format!("arity mismatch: expected {}, got {}", params.len(), argv.len()));
                }
                for (p, v) in params.iter().zip(argv.iter()) {
                    call_env.insert(p.clone(), v.clone());
                }
                eval_func_body(body, &mut call_env, ops)
            } else {
                Err(format!("undefined function: {}", name))
            }
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
            if let Some(Value::Func(params, body, fenv)) = env.get(fname) {
                let mut call_env = fenv.clone();
                if params.len() != 2 {
                    return Err("custom operator must bind to binary function".to_string());
                }
                call_env.insert(params[0].clone(), va);
                call_env.insert(params[1].clone(), vb);
                eval_func_body(body, &mut call_env, ops)
            } else {
                Err(format!("undefined operator function: {}", fname))
            }
        }
        Expr::Group(e) => eval(e, env, ops),
        Expr::Map(list_expr, func_name) => {
            let lv = eval(list_expr, env, ops)?;
            match lv {
                Value::List(xs, _) => {
                    if let Some(Value::Func(params, body, fenv)) = env.get(func_name) {
                        if params.len() != 1 {
                            return Err("map function must be unary".to_string());
                        }
                        let mut out = Vec::new();
                        for x in xs {
                            let mut call_env = fenv.clone();
                            call_env.insert(params[0].clone(), x);
                            let r = eval_func_body(body, &mut call_env, ops)?;
                            out.push(r);
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

fn eval_func_body(
    stmts: &[Stmt],
    env: &mut HashMap<String, Value>,
    ops: &HashMap<String, String>,
) -> Result<Value, String> {
    for stmt in stmts {
        match stmt {
            Stmt::Return(opt_val) => {
                if let Some(e) = opt_val {
                    return eval(e, env, ops);
                } else {
                    // Return void/none
                    // Assuming empty string or specific value for void?
                    // For now, let's return Int(0) or similar dummy, or handle Void type.
                    // But eval returns Value.
                    // Let's return Value::Int(0) for now as void.
                    return Ok(Value::Int(0));
                }
            }
            Stmt::Let(n, e) => {
                let v = eval(e, env, ops)?;
                env.insert(n.clone(), v);
            }
            Stmt::PrintExpr(e) => {
                let v = eval(e, env, ops)?;
                println!("{}", to_string(&v)); // Functions print directly for now or could collect output
            }
             _ => return Err("unsupported statement in function block".to_string()),
        }
    }
    Ok(Value::Int(0)) // Default return
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

fn expr_to_string(e: &Expr) -> Option<String> {
    match e {
        Expr::Str(s) => Some(s.clone()),
        Expr::Var(s) => Some(s.clone()),
        Expr::Sym(s) => Some(s.clone()),
        _ => None,
    }
}

fn list_expr_items(e: &Expr) -> Option<Vec<Expr>> {
    match e {
        Expr::List(items) => Some(items.iter().map(|li| li.value.clone()).collect()),
        _ => None,
    }
}

fn list_to_stmt(items: &[Expr]) -> Result<Stmt, String> {
    if items.is_empty() {
        return Err("empty stmt list".to_string());
    }
    match &items[0] {
        Expr::Var(name) => {
            let mut converted = items.to_vec();
            converted[0] = Expr::Sym(name.clone());
            return list_to_stmt(&converted);
        }
        Expr::Sym(s) => {
            match s.as_str() {
                "let" => {
                    if items.len() != 3 { return Err("let expects name and expr".to_string()); }
                    let name = expr_to_string(&items[1]).ok_or_else(|| "let name must be string or ident".to_string())?;
                    Ok(Stmt::Let(name, items[2].clone()))
                }
                "print" => {
                    if items.len() != 2 { return Err("print expects expr".to_string()); }
                    Ok(Stmt::PrintExpr(items[1].clone()))
                }
                "print_template" => {
                    if items.len() == 2 {
                        let tpl = expr_to_string(&items[1]).ok_or_else(|| "template must be string".to_string())?;
                        Ok(Stmt::PrintTemplate(tpl, None))
                    } else if items.len() == 3 {
                        let tpl = expr_to_string(&items[1]).ok_or_else(|| "template must be string".to_string())?;
                        Ok(Stmt::PrintTemplate(tpl, Some(items[2].clone())))
                    } else {
                        Err("print_template expects 1 or 2 args".to_string())
                    }
                }
                "list_types" => Ok(Stmt::ListTypes),
                "list_type" => {
                    if items.len() != 2 { return Err("list_type expects name".to_string()); }
                    let name = expr_to_string(&items[1]).ok_or_else(|| "type name must be string".to_string())?;
                    Ok(Stmt::ListType(name))
                }
                "reg_operator" => {
                    if items.len() != 3 { return Err("reg_operator expects op and function".to_string()); }
                    let op = expr_to_string(&items[1]).ok_or_else(|| "op must be string".to_string())?;
                    let fname = expr_to_string(&items[2]).ok_or_else(|| "function name must be string".to_string())?;
                    Ok(Stmt::RegOp(op, fname))
                }
                "return" => {
                    if items.len() == 1 {
                        Ok(Stmt::Return(None))
                    } else if items.len() == 2 {
                        Ok(Stmt::Return(Some(items[1].clone())))
                    } else {
                        Err("return expects 0 or 1 arg".to_string())
                    }
                }
                "call" => {
                    if items.len() < 3 { return Err("call expects type, op and args".to_string()); }
                    let t = expr_to_string(&items[1]).ok_or_else(|| "type must be string".to_string())?;
                    let op = expr_to_string(&items[2]).ok_or_else(|| "op must be string".to_string())?;
                    let mut args = Vec::new();
                    for e in items.iter().skip(3) {
                        args.push(e.clone());
                    }
                    Ok(Stmt::Call(t, op, args))
                }
                "let_func" => {
                    if items.len() != 4 { return Err("let_func expects name, params, body".to_string()); }
                    let name = expr_to_string(&items[1]).ok_or_else(|| "func name must be string".to_string())?;
                    let params = list_expr_items(&items[2]).ok_or_else(|| "params must be list".to_string())?;
                    let mut ps = Vec::new();
                    for p in params {
                        let s = expr_to_string(&p).ok_or_else(|| "param must be string or ident".to_string())?;
                        ps.push(s);
                    }
                    let bl = list_expr_items(&items[3]).ok_or_else(|| "body must be list".to_string())?;
                    let mut body_stmts = Vec::new();
                    for e in bl {
                        if let Some(inner) = list_expr_items(&e) {
                            body_stmts.push(list_to_stmt(&inner)?);
                        } else {
                            return Err("body items must be list".to_string());
                        }
                    }
                    Ok(Stmt::LetFunc(name, ps, body_stmts))
                }
                "expr" => {
                    if items.len() != 2 { return Err("expr expects 1 arg".to_string()); }
                    Ok(Stmt::ExprStmt(items[1].clone()))
                }
                "block" => {
                    if items.len() == 2 {
                        let header_items = list_expr_items(&items[0]).ok_or_else(|| "block header must be list".to_string())?;
                        if header_items.len() < 2 { return Err("block header must contain name and meta".to_string()); }
                        let name = expr_to_string(&header_items[1]).ok_or_else(|| "block name must be string".to_string())?;
                        let meta_expr = header_items.get(2).cloned().unwrap_or(Expr::List(vec![]));
                        let meta_vec = match meta_expr {
                            Expr::List(v) => v,
                            _ => return Err("block meta must be list".to_string()),
                        };
                        let body_items = list_expr_items(&items[1]).ok_or_else(|| "block body must be list".to_string())?;
                        let mut stmts = Vec::new();
                        for e in body_items {
                            if let Some(inner) = list_expr_items(&e) {
                                stmts.push(list_to_stmt(&inner)?);
                            } else {
                                return Err("block body items must be list".to_string());
                            }
                        }
                        Ok(Stmt::Block(name, meta_vec, stmts))
                    } else if items.len() == 4 {
                        let name = expr_to_string(&items[1]).ok_or_else(|| "block name must be string".to_string())?;
                        let meta_expr = items[2].clone();
                        let meta_vec = match meta_expr {
                            Expr::List(v) => v,
                            _ => return Err("block meta must be list".to_string()),
                        };
                        let body_items = list_expr_items(&items[3]).ok_or_else(|| "block body must be list".to_string())?;
                        let mut stmts = Vec::new();
                        for e in body_items {
                            if let Some(inner) = list_expr_items(&e) {
                                stmts.push(list_to_stmt(&inner)?);
                            } else {
                                return Err("block body items must be list".to_string());
                            }
                        }
                        Ok(Stmt::Block(name, meta_vec, stmts))
                    } else {
                        Err("block expects header and body".to_string())
                    }
                }
                _ => Err("unknown directive".to_string()),
            }
        }
        Expr::List(h) => {
            let mut hv = Vec::new();
            for li in h {
                hv.push(li.value.clone());
            }
            let mut items2 = Vec::new();
            items2.extend(hv);
            items2.extend(items.iter().skip(1).cloned());
            list_to_stmt(&items2)
        }
        _ => Err("invalid stmt list".to_string()),
    }
}

pub fn stmt_to_list_expr(stmt: &Stmt) -> Expr {
    match stmt {
        Stmt::Let(name, expr) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("let".to_string()) },
                ListItem { key: None, value: Expr::Str(name.clone()) },
                ListItem { key: None, value: expr.clone() },
            ])
        }
        Stmt::PrintTemplate(tpl, arg) => {
            let mut items = vec![
                ListItem { key: None, value: Expr::Sym("print_template".to_string()) },
                ListItem { key: None, value: Expr::Str(tpl.clone()) },
            ];
            if let Some(e) = arg {
                items.push(ListItem { key: None, value: e.clone() });
            }
            Expr::List(items)
        }
        Stmt::PrintExpr(expr) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("print".to_string()) },
                ListItem { key: None, value: expr.clone() },
            ])
        }
        Stmt::ListTypes => {
            Expr::List(vec![ListItem { key: None, value: Expr::Sym("list_types".to_string()) }])
        }
        Stmt::ListType(name) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("list_type".to_string()) },
                ListItem { key: None, value: Expr::Str(name.clone()) },
            ])
        }
        Stmt::RegOp(op, fname) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("reg_operator".to_string()) },
                ListItem { key: None, value: Expr::Str(op.clone()) },
                ListItem { key: None, value: Expr::Str(fname.clone()) },
            ])
        }
        Stmt::LetFunc(name, params, body) => {
            let params_expr = Expr::List(params.iter().map(|p| ListItem { key: None, value: Expr::Str(p.clone()) }).collect());
            let body_expr = Expr::List(body.iter().map(|s| ListItem { key: None, value: stmt_to_list_expr(s) }).collect());
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("let_func".to_string()) },
                ListItem { key: None, value: Expr::Str(name.clone()) },
                ListItem { key: None, value: params_expr },
                ListItem { key: None, value: body_expr },
            ])
        }
        Stmt::Return(opt) => {
            let mut items = vec![ListItem { key: None, value: Expr::Sym("return".to_string()) }];
            if let Some(e) = opt {
                items.push(ListItem { key: None, value: e.clone() });
            }
            Expr::List(items)
        }
        Stmt::Call(t, op, args) => {
            let mut items = vec![
                ListItem { key: None, value: Expr::Sym("call".to_string()) },
                ListItem { key: None, value: Expr::Str(t.clone()) },
                ListItem { key: None, value: Expr::Str(op.clone()) },
            ];
            for a in args {
                items.push(ListItem { key: None, value: a.clone() });
            }
            Expr::List(items)
        }
        Stmt::RunList(items) => {
            Expr::List(items.iter().map(|e| ListItem { key: None, value: e.clone() }).collect())
        }
        Stmt::ExprStmt(e) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("expr".to_string()) },
                ListItem { key: None, value: e.clone() },
            ])
        }
        Stmt::Block(name, meta, body) => {
            let header = Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("block".to_string()) },
                ListItem { key: None, value: Expr::Str(name.clone()) },
                ListItem { key: None, value: Expr::List(meta.clone()) },
            ]);
            let body_list = Expr::List(body.iter().map(|s| ListItem { key: None, value: stmt_to_list_expr(s) }).collect());
            Expr::List(vec![
                ListItem { key: None, value: header },
                ListItem { key: None, value: body_list },
            ])
        }
    }
}
