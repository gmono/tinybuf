#[derive(Debug, Clone, PartialEq)]
pub enum Stmt {
    Let(String, Expr),
    PrintTemplate(String, Option<Expr>),
    PrintExpr(Expr),
    ListTypes,
    ListType(String),
    RegOp(String, String),
    LetFunc(String, Vec<(Option<String>, String)>, Expr),
    LetFuncBlock(String, Vec<(Option<String>, String)>, Vec<Stmt>),
  Return(Expr),
    Call(String, String, Vec<Expr>),
    Run(Expr),
    ExprStmt(Expr),
}

#[derive(Debug, Clone, PartialEq)]
pub struct ListItem {
    pub key: Option<String>,
    pub value: Expr,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    Int(i64),
    Str(String),
    Var(String),
    Sym(String),
    SymToStr(Box<Expr>),
    List(Vec<ListItem>),
    Call(Box<Expr>, Vec<Expr>),
    Add(Box<Expr>, Box<Expr>),
    Sub(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    Div(Box<Expr>, Box<Expr>),
    Mod(Box<Expr>, Box<Expr>),
    Pow(Box<Expr>, Box<Expr>),
    Custom(Box<Expr>, String, Box<Expr>),
    Map(Box<Expr>, String),
    MethodCall(Box<Expr>, String, Vec<Expr>),
    Group(Box<Expr>),
    Index(Box<Expr>, Box<Expr>),
}

pub fn parse_lisp_group(items: Vec<ListItem>) -> Expr {
    // If it's a single item, treat as Group (unless it's a list explicitly?)
    if items.len() == 1 && items[0].key.is_none() {
        // If it's a keyword, keep as List ([+], [let])?
        // But (a) should be Group(a).
        // (+ 1 2) is List. (+) is List.
        // But (a) is Group.
        match &items[0].value {
            Expr::Var(s) if s == "let" || s == "print" || s == "run" => return Expr::List(items),
            _ => return Expr::Group(Box::new(items[0].value.clone())),
        }
    }

    // Check for infix expression: (a + b)
    if items.len() == 3 && items[1].key.is_none() {
        if let Expr::Var(op) = &items[1].value {
            let lhs = items[0].value.clone();
            let rhs = items[2].value.clone();
            match op.as_str() {
                "+" => return Expr::Add(Box::new(lhs), Box::new(rhs)),
                "-" => return Expr::Sub(Box::new(lhs), Box::new(rhs)),
                "*" => return Expr::Mul(Box::new(lhs), Box::new(rhs)),
                "/" => return Expr::Div(Box::new(lhs), Box::new(rhs)),
                "%" => return Expr::Mod(Box::new(lhs), Box::new(rhs)),
                "**" => return Expr::Pow(Box::new(lhs), Box::new(rhs)),
                "|" => {
                     if let Expr::Var(f) = rhs {
                         return Expr::Map(Box::new(lhs), f);
                     } else if let Expr::Sym(f) = rhs {
                         return Expr::Map(Box::new(lhs), f);
                     }
                }
                _ => {}
            }
        }
    }
    
    // Check for longer infix? (a + b + c) -> left assoc?
    // For now, let's stick to simple or List.
    // If we want to support complex math in (), we should implement full parsing.
    // But for this task, simple (a+b) covers most cases.
    
    Expr::List(items)
}
