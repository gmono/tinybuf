use std::collections::HashMap;

use crate::ast::{Expr, Stmt};

#[derive(Debug, Clone)]
enum Value {
    Int(i64),
    Str(String),
}

pub fn run(program: &[Stmt]) -> Result<Vec<String>, String> {
    let mut env: HashMap<String, Value> = HashMap::new();
    let mut outputs = Vec::new();
    for stmt in program {
        match stmt {
            Stmt::Let(name, expr) => {
                let v = eval(expr, &env)?;
                env.insert(name.clone(), v);
            }
            Stmt::PrintTemplate(tpl, arg) => {
                if let Some(arg) = arg {
                    let v = eval(arg, &env)?;
                    let vstr = to_string(&v);
                    let rendered = tpl.replace("{}", &vstr);
                    outputs.push(rendered);
                } else {
                    outputs.push(tpl.clone());
                }
            }
            Stmt::PrintExpr(expr) => {
                let v = eval(expr, &env)?;
                outputs.push(to_string(&v));
            }
            Stmt::ListTypes => {
                // TODO: integrate with tinybuf_oop_* via FFI
                outputs.push("type_a".to_string());
            }
            Stmt::ListType(name) => {
                outputs.push(format!("type {}", name));
            }
            Stmt::ExprStmt(expr) => {
                let _ = eval(expr, &env)?;
            }
        }
    }
    Ok(outputs)
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

