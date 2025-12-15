use std::io::{self, Write};
use tinybuf_script::{parse_program, Interpreter, ensure_oop_demo_registered};
use tinybuf_script::shell_transform_line;

fn main() {
    ensure_oop_demo_registered();
    let mut interp = Interpreter::new();
    println!("tinybuf_script shell");
    loop {
        print!("tbs> ");
        let _ = io::stdout().flush();
        let mut line = String::new();
        if io::stdin().read_line(&mut line).unwrap_or(0) == 0 {
            break;
        }
        let line = line.trim();
        if line.is_empty() {
            continue;
        }
        if line == ":quit" || line == ":exit" {
            break;
        }
        if line == ":help" {
            println!("commands: :help, :quit, :types, :type NAME");
            continue;
        }
        let src = shell_transform_line(line);
        match parse_program(&src) {
            Ok(ast) => match interp.run(&ast) {
                Ok(outputs) => {
                    for o in outputs {
                        println!("{}", o);
                    }
                }
                Err(e) => {
                    println!("error: {}", e);
                }
            },
            Err(e) => {
                println!("parse error: {}", e);
            }
        }
    }
}
