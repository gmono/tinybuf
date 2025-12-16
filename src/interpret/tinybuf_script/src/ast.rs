#[derive(Debug, Clone, PartialEq)]
pub enum Stmt {
    Let(String, Expr),
    PrintTemplate(String, Option<Expr>),
    PrintExpr(Expr),
    ListTypes,
    ListType(String),
    RegOp(String, String),
    LetFunc(String, Vec<String>, Expr),
    LetFuncBlock(String, Vec<String>, Vec<Stmt>, Expr),
    Return(Expr),
    Call(String, String, Vec<Expr>),
    RunList(Vec<Expr>),
    ExprStmt(Expr),
    Test(Vec<Stmt>),
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
    List(Vec<ListItem>),
    Call(String, Vec<Expr>),
    Add(Box<Expr>, Box<Expr>),
    Sub(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    Div(Box<Expr>, Box<Expr>),
    Mod(Box<Expr>, Box<Expr>),
    Pow(Box<Expr>, Box<Expr>),
    Custom(Box<Expr>, String, Box<Expr>),
    Map(Box<Expr>, String),
    Group(Box<Expr>),
    Index(Box<Expr>, Box<Expr>),
}
