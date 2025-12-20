use std::collections::HashMap;
use std::ffi::CStr;
use crate::ast::{Stmt, Expr, ListItem};

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Str(String),
    List(Vec<Value>, std::collections::HashMap<String, usize>),
    Func(Vec<String>, Vec<Stmt>, HashMap<String, Value>),
    Block(Vec<Stmt>),
}

#[derive(Debug, Clone)]
pub enum ControlFlow {
    Continue,
    Return(Option<Value>),
    Break(Option<String>),
    Next,
}

type BlockHandler = fn(&mut Interpreter, Vec<ListItem>, Vec<Stmt>) -> Result<(Vec<String>, ControlFlow), String>;

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
        interpreter.register_block_handler("loop", Self::handle_loop);
        interpreter
    }

    pub fn register_block_handler(&mut self, name: &str, handler: BlockHandler) {
        self.block_handlers.insert(name.to_string(), handler);
    }

    fn handle_test(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<(Vec<String>, ControlFlow), String> {
        if !interp.test_mode { return Ok((vec![], ControlFlow::Continue)); }
        let saved_env = interp.env.clone();
        let saved_ops = interp.ops.clone();
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let result = interp.run_block(&body);
        interp.test_mode = saved_mode;
        interp.env = saved_env;
        interp.ops = saved_ops;
        result
    }

    fn handle_test_init(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<(Vec<String>, ControlFlow), String> {
        if !interp.test_mode { return Ok((vec![], ControlFlow::Continue)); }
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let result = interp.run_block(&body);
        interp.test_mode = saved_mode;
        result
    }

    fn handle_notest(interp: &mut Interpreter, _meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<(Vec<String>, ControlFlow), String> {
        if interp.test_mode { return Ok((vec![], ControlFlow::Continue)); }
        let saved_mode = interp.test_mode;
        interp.test_mode = false;
        let result = interp.run_block(&body);
        interp.test_mode = saved_mode;
        result
    }
    fn handle_loop(interp: &mut Interpreter, meta: Vec<ListItem>, body: Vec<Stmt>) -> Result<(Vec<String>, ControlFlow), String> {
        // Extract persist and step blocks from meta
        let mut persist_stmts: Vec<Stmt> = Vec::new();
        let mut step_stmts: Vec<Stmt> = Vec::new();
        let mut config: HashMap<String, String> = HashMap::new();

        for li in &meta {
            match (&li.key, &li.value) {
                (Some(k), v) if k == "persist" => {
                    let v = unwrap_group(v);
                    if let Expr::List(items) = v {
                        let mut out = Vec::new();
                        let is_single_stmt = items.first().map(|li| !matches!(li.value, Expr::List(_))).unwrap_or(false);
                        
                        if is_single_stmt && !items.is_empty() {
                             if let Some(inner) = list_expr_items(v) {
                                  out.push(list_to_stmt(&inner)?);
                             }
                        } else {
                            for e in items {
                                if let Some(inner) = list_expr_items(&e.value) {
                                    out.push(list_to_stmt(&inner)?);
                                }
                            }
                        }
                        persist_stmts = out;
                    }
                }
                (Some(k), v) if k == "step" => {
                    let v = unwrap_group(v);
                    if let Expr::List(items) = v {
                        let mut out = Vec::new();
                        let is_single_stmt = items.first().map(|li| !matches!(li.value, Expr::List(_))).unwrap_or(false);

                        if is_single_stmt && !items.is_empty() {
                             if let Some(inner) = list_expr_items(v) {
                                  out.push(list_to_stmt(&inner)?);
                             }
                        } else {
                            for e in items {
                                if let Some(inner) = list_expr_items(&e.value) {
                                    out.push(list_to_stmt(&inner)?);
                                }
                            }
                        }
                        step_stmts = out;
                    }
                }
                (Some(k), Expr::Str(v)) => {
                    config.insert(k.clone(), v.clone());
                }
                 _ => { /* ignore */ }
            }
        }
        
        // Handle persist initialization (run once in current scope)
        // Wait, persist variables should be retained.
        // If we run them in current scope, they pollute current scope but are retained.
        // Loop usually creates a new scope.
        // "persist variables are retained across body iterations"
        // This implies they are local to the loop but outlive iterations.
        // So we should create a loop scope.
        
        // Initialize persist env
        let saved_env = interp.env.clone(); // Backup to restore later if we were modifying interp.env directly
        // Actually, we can work on `interp.env` but use a temporary env for body?
        // No, we need an intermediate scope `persist_env`.
        
        // Create persist scope
        let mut persist_env = interp.env.clone();
        
        // Run persist stmts on persist_env
        // We need to swap interp.env with persist_env to use `run_block`
        interp.env = persist_env;
        let (p_out, p_flow) = interp.run_block(&persist_stmts)?;
        
        if let ControlFlow::Return(_) = p_flow { return Ok((p_out, p_flow)); }
        // Break/Next in persist block? Probably error or stop.
        if matches!(p_flow, ControlFlow::Break(_) | ControlFlow::Next) {
             // restore env
             interp.env = saved_env;
             return Ok((p_out, p_flow));
        }
        
        persist_env = interp.env.clone(); // capture modified env
        interp.env = saved_env.clone(); // restore for now
        
        let mut outputs = p_out;
        
        // Determine iterations? Infinite unless break?
        // "Decide iteration count" - previously 2 or 3 for test.
        // But real loop should run until break.
        // The prompt says "support loop ...".
        // `more.tbs` shows `if a>1:next`, `if b>2:break`.
        // So it relies on break.
        
        let max_iters = if interp.test_mode { 100 } else { 1000 };
        
        let label = config.get("name").cloned();
        
        for _ in 0..max_iters {
            // Prepare body env
            let body_env = persist_env.clone();
            
            // Run body
            interp.env = body_env;
            let (b_out, b_flow) = interp.run_block(&body)?;
            outputs.extend(b_out);
            
            // Handle flow
            match b_flow {
                ControlFlow::Return(v) => {
                    interp.env = saved_env;
                    return Ok((outputs, ControlFlow::Return(v)));
                }
                ControlFlow::Break(l) => {
                     // Check label
                     if let Some(target) = l {
                         if let Some(my_label) = &label {
                             if *my_label == target {
                                 // Break this loop
                                 break;
                             } else {
                                 // Break outer loop
                                 interp.env = saved_env;
                                 return Ok((outputs, ControlFlow::Break(Some(target))));
                             }
                         } else {
                             // I don't have label, propagate
                             interp.env = saved_env;
                             return Ok((outputs, ControlFlow::Break(Some(target))));
                         }
                     } else {
                         // Unlabeled break, break this loop
                         break;
                     }
                }
                ControlFlow::Next => {
                    // Continue to step
                }
                ControlFlow::Continue => {
                    // Normal completion
                }
            }
            
            // Run step
            // Step runs in persist_env (updates counters)
            interp.env = persist_env;
            let (s_out, s_flow) = interp.run_block(&step_stmts)?;
            outputs.extend(s_out);
            persist_env = interp.env.clone(); // Capture updates
            
             match s_flow {
                ControlFlow::Return(v) => {
                    interp.env = saved_env;
                    return Ok((outputs, ControlFlow::Return(v)));
                }
                ControlFlow::Break(l) => {
                     // Handle break in step same as body
                     if let Some(target) = l {
                         if let Some(my_label) = &label {
                             if *my_label == target { break; } 
                             else { interp.env = saved_env; return Ok((outputs, ControlFlow::Break(Some(target)))); }
                         } else { interp.env = saved_env; return Ok((outputs, ControlFlow::Break(Some(target)))); }
                     } else { break; }
                }
                ControlFlow::Next => { /* ignore or continue? */ }
                ControlFlow::Continue => {}
            }
            
            // Restore saved_env for safety between iterations? No we loop.
        }
        
        interp.env = saved_env;
        Ok((outputs, ControlFlow::Continue))
    }

    pub fn run(&mut self, program: &[Stmt]) -> Result<Vec<String>, String> {
        let (out, flow) = self.run_block(program)?;
         match flow {
            ControlFlow::Continue => Ok(out),
            ControlFlow::Return(_) => Err("Return not allowed at top level".to_string()),
            ControlFlow::Break(_) => Err("Break not allowed at top level".to_string()),
            ControlFlow::Next => Err("Next not allowed at top level".to_string()),
        }
    }
    
    pub fn run_block(&mut self, program: &[Stmt]) -> Result<(Vec<String>, ControlFlow), String> {
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
                    return Ok((outputs, ControlFlow::Return(Some(v))));
                } else {
                    return Ok((outputs, ControlFlow::Return(None)));
                }
            }
            Stmt::ExprStmt(expr) => {
                let _ = eval(expr, &self.env, &self.ops)?;
            }
            Stmt::Block(name, meta, stmts) => {
                if let Some(&handler) = self.block_handlers.get(name) {
                    let (out, flow) = handler(self, meta.clone(), stmts.clone())?;
                    outputs.extend(out);
                    if !matches!(flow, ControlFlow::Continue) {
                        return Ok((outputs, flow));
                    }
                } else {
                    return Err(format!("Unknown block type: {}", name));
                }
            }
            Stmt::If(cond, then_stmt, else_stmt) => {
                let v = eval(cond, &self.env, &self.ops)?;
                let is_true = match v {
                    Value::Int(i) => i != 0,
                    Value::Str(s) => !s.is_empty(),
                     _ => false,
                };
                if is_true {
                    let (out, flow) = self.run_block(std::slice::from_ref(then_stmt))?;
                    outputs.extend(out);
                    if !matches!(flow, ControlFlow::Continue) {
                        return Ok((outputs, flow));
                    }
                } else if let Some(else_s) = else_stmt {
                    let (out, flow) = self.run_block(std::slice::from_ref(else_s))?;
                    outputs.extend(out);
                    if !matches!(flow, ControlFlow::Continue) {
                        return Ok((outputs, flow));
                    }
                }
            }
            Stmt::Break(label) => {
                return Ok((outputs, ControlFlow::Break(label.clone())));
            }
            Stmt::Next => {
                return Ok((outputs, ControlFlow::Next));
            }
        }
    }
        Ok((outputs, ControlFlow::Continue))
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
                
                // Hack: We need mut access to self to call run_block, but self is borrowed.
                // However, run_block needs self.env and self.ops and self.block_handlers.
                // We have call_env which we want to use as env.
                // We can clone self.ops and self.block_handlers?
                // self.block_handlers contains fn pointers (Copy/Clone).
                // self.ops is HashMap<String, String>.
                // So we can create a temporary Interpreter?
                // But block handlers expect &mut Interpreter.
                // If we create a temp one, it's fine.
                // BUT if block handler modifies Interpreter state (e.g. registers new op or changes env),
                // we want that to persist?
                // Usually functions are isolated except for return value.
                // But `RegOp` inside function?
                // If we use temp interpreter, `RegOp` affects temp ops.
                // If we want it to affect global ops, we need to copy back.
                // But `eval_func_body` passed `ops` as immutable ref.
                // So `RegOp` was NOT supported/allowed inside functions in old implementation?
                // `eval_func_body` did NOT support `RegOp`.
                // So using temp interpreter with cloned ops is safe/compatible.
                
                let mut temp_interp = Interpreter {
                    env: call_env,
                    ops: self.ops.clone(),
                    block_handlers: self.block_handlers.clone(),
                    test_mode: self.test_mode,
                };
                
                let (out, flow) = temp_interp.run_block(body_stmts)?;
                // Ignore output strings? Or print them?
                // Usually functions shouldn't print unless they use Print stmt.
                // `run_block` collects outputs.
                // We should probably print them to stdout if we are not capturing?
                // But `call_func` returns `Value`.
                // The output strings are side effect.
                for s in out {
                    println!("{}", s);
                }
                
                match flow {
                    ControlFlow::Return(val) => Ok(val.unwrap_or(Value::Int(0))),
                    ControlFlow::Continue => Ok(Value::Int(0)), // Default return
                    _ => Err("Unexpected control flow in function".to_string()),
                }
            }
            _ => Err(format!("undefined function: {}", name)),
        }
    }
}

fn unwrap_group(e: &Expr) -> &Expr {
    if let Expr::Group(inner) = e {
        unwrap_group(inner)
    } else {
        e
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

pub fn eval_expr_for_tests(expr: &Expr) -> Result<Value, String> {
    let env = std::collections::HashMap::new();
    let ops = std::collections::HashMap::new();
    eval(expr, &env, &ops)
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
        Value::Block(_) => "<block>".to_string(),
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
        Expr::Block(stmts) => {
             let mut block_env = env.clone();
             eval_block_expr(stmts, &mut block_env, ops)
        },
        Expr::BlockNoValue(stmts) => {
             let mut block_env = env.clone();
             let _ = eval_block_expr(stmts, &mut block_env, ops)?;
             Ok(Value::Int(0))
        },
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
                let arg_val = eval(&args[0], env, ops)?;
                match arg_val {
                    Value::List(items, _keys) => {
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
                    }
                    Value::Str(s) => {
                        if let Some(val) = env.get(&s) {
                            return Ok(val.clone());
                        } else {
                            return Err(format!("undefined variable: {}", s));
                        }
                    }
                    _ => {
                        return Err("value_of expects a list or sym/string".to_string());
                    }
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
        Expr::Gt(a, b) => {
            let va = eval(a, env, ops)?;
            let vb = eval(b, env, ops)?;
            match (va, vb) {
                (Value::Int(x), Value::Int(y)) => Ok(Value::Int(if x > y { 1 } else { 0 })),
                _ => Err("type error: > expects integers".to_string()),
            }
        }
        Expr::Lt(a, b) => {
            let va = eval(a, env, ops)?;
            let vb = eval(b, env, ops)?;
            match (va, vb) {
                (Value::Int(x), Value::Int(y)) => Ok(Value::Int(if x < y { 1 } else { 0 })),
                _ => Err("type error: < expects integers".to_string()),
            }
        }
        Expr::If(cond, then_expr, else_expr) => {
             let v = eval(cond, env, ops)?;
             let is_true = match v {
                 Value::Int(i) => i != 0,
                 Value::Str(s) => !s.is_empty(),
                 _ => false,
             };
             if is_true {
                 eval(then_expr, env, ops)
             } else if let Some(else_e) = else_expr {
                 eval(else_e, env, ops)
             } else {
                 Ok(Value::Int(0)) // Void/Nil
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

fn eval_block_expr(
    stmts: &[Stmt],
    env: &mut HashMap<String, Value>,
    ops: &HashMap<String, String>,
) -> Result<Value, String> {
    let mut last_val = Value::Int(0);
    for stmt in stmts {
        match stmt {
            Stmt::Let(n, e) => {
                let v = eval(e, env, ops)?;
                env.insert(n.clone(), v);
            }
            Stmt::PrintExpr(e) => {
                let v = eval(e, env, ops)?;
                println!("{}", to_string(&v));
            }
            Stmt::ExprStmt(e) => {
                last_val = eval(e, env, ops)?;
            }
            Stmt::Return(opt_e) => {
                if let Some(e) = opt_e {
                    return eval(e, env, ops);
                } else {
                    return Ok(Value::Int(0));
                }
            }
            Stmt::If(cond, then_stmt, else_stmt) => {
                 let v = eval(cond, env, ops)?;
                 let is_true = match v {
                     Value::Int(i) => i != 0,
                     Value::Str(s) => !s.is_empty(),
                     _ => false,
                 };
                 if is_true {
                     last_val = eval_block_expr(std::slice::from_ref(then_stmt), env, ops)?;
                 } else if let Some(else_s) = else_stmt {
                     last_val = eval_block_expr(std::slice::from_ref(else_s), env, ops)?;
                 }
            }
            _ => return Err("unsupported statement in expression block".to_string()),
        }
    }
    Ok(last_val)
}

fn eval_func_body(
    stmts: &[Stmt],
    env: &mut HashMap<String, Value>,
    ops: &HashMap<String, String>,
) -> Result<Value, String> {
    eval_block_expr(stmts, env, ops)
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
                "break" => {
                    if items.len() == 1 {
                        Ok(Stmt::Break(None))
                    } else {
                        Err("break expects no arguments".to_string())
                    }
                }
                "break_to" => {
                    if items.len() == 2 {
                        let label = expr_to_string(&items[1]).ok_or_else(|| "label must be string or ident".to_string())?;
                        Ok(Stmt::Break(Some(label)))
                    } else {
                        Err("break_to expects 1 arg".to_string())
                    }
                }
                "next" => {
                    if items.len() == 1 {
                        Ok(Stmt::Next)
                    } else {
                        Err("next expects no arguments".to_string())
                    }
                }
                "if" => {
                    if items.len() == 3 {
                        let cond = items[1].clone();
                        if let Some(inner) = list_expr_items(&items[2]) {
                            let then_stmt = list_to_stmt(&inner)?;
                            Ok(Stmt::If(cond, Box::new(then_stmt), None))
                        } else {
                            Err("if then must be a list-encoded stmt".to_string())
                        }
                    } else if items.len() == 4 {
                        let cond = items[1].clone();
                        let then_stmt = if let Some(inner) = list_expr_items(&items[2]) {
                            list_to_stmt(&inner)?
                        } else {
                            return Err("if then must be a list-encoded stmt".to_string());
                        };
                        let else_stmt = if let Some(inner) = list_expr_items(&items[3]) {
                            list_to_stmt(&inner)?
                        } else {
                            return Err("if else must be a list-encoded stmt".to_string());
                        };
                        Ok(Stmt::If(cond, Box::new(then_stmt), Some(Box::new(else_stmt))))
                    } else {
                         Err("if expects 2 or 3 args".to_string())
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
        Stmt::Break(None) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("break".to_string()) },
            ])
        }
        Stmt::Break(Some(lbl)) => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("break_to".to_string()) },
                ListItem { key: None, value: Expr::Str(lbl.clone()) },
            ])
        }
        Stmt::Next => {
            Expr::List(vec![
                ListItem { key: None, value: Expr::Sym("next".to_string()) },
            ])
        }
        Stmt::If(cond, then_s, else_s) => {
            let then_expr = stmt_to_list_expr(then_s);
            let mut items = vec![
                ListItem { key: None, value: Expr::Sym("if".to_string()) },
                ListItem { key: None, value: cond.clone() },
                ListItem { key: None, value: then_expr },
            ];
            if let Some(e) = else_s {
                items.push(ListItem { key: None, value: stmt_to_list_expr(e) });
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
