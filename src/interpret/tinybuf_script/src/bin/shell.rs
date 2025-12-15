use std::io::{self, Write};
use tinybuf_script::{parse_program, Interpreter};

fn main() {
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
        let src = if line == ":types" {
            "list types".to_string()
        } else if let Some(rest) = line.strip_prefix(":type ") {
            let mut s = String::from("list type ");
            s.push_str(rest.trim());
            s
        } else {
            line.to_string()
        };
        let src = if src.ends_with(';') || src.ends_with('\n') { src } else { format!("{src}\n") };
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
