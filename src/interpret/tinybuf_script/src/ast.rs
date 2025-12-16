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
    RunList(Vec<Expr>),
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
    Call(String, Vec<Expr>),
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
