#[derive(Debug, Clone, PartialEq)]
pub enum Stmt {
    Let(String, Expr),
    PrintTemplate(String, Option<Expr>),
    PrintExpr(Expr),
    ListTypes,
    ListType(String),
    Call(String, String, Vec<Expr>),
    ExprStmt(Expr),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Expr {
    Int(i64),
    Str(String),
    Var(String),
    Add(Box<Expr>, Box<Expr>),
    Sub(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    Div(Box<Expr>, Box<Expr>),
    Group(Box<Expr>),
}
