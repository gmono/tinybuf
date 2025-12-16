use std::io::{self, Write};
use tinybuf_script::{parse_program, Interpreter, ensure_oop_demo_registered};
use tinybuf_script::shell_transform_line;

fn main() {
    ensure_oop_demo_registered();
    
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        let (filename, test_mode) = if args[1] == "test" && args.len() > 2 {
            (&args[2], true)
        } else {
            (&args[1], false)
        };

        let src = match std::fs::read_to_string(filename) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("error: could not read file '{}': {}", filename, e);
                std::process::exit(1);
            }
        };
        
        let mut interp = Interpreter::new();
        interp.test_mode = test_mode;
        match parse_program(&src) {
            Ok(ast) => match interp.run(&ast) {
                Ok(outputs) => {
                    for o in outputs {
                        println!("{}", o);
                    }
                }
                Err(e) => {
                    eprintln!("runtime error: {}", e);
                    std::process::exit(1);
                }
            },
            Err(e) => {
                eprintln!("parse error: {}", e);
                std::process::exit(1);
            }
        }
        return;
    }

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
