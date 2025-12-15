use logos::Logos;

#[derive(Debug, Clone, PartialEq)]
pub enum Token {
    Let,
    Print,
    List,
    Types,
    Type,
    Ident(String),
    Number(i64),
    StringLit(String),
    Eq,
    Plus,
    Minus,
    Star,
    Slash,
    LParen,
    RParen,
    Semi,
    Newline,
}

#[derive(Logos)]
#[logos(skip r"[ \t\r\f]+")]
enum LexToken {
    #[regex("//[^\n]*")]
    Comment,

    #[token("\n")]
    Newline,

    #[token("let")]
    Let,
    #[token("print")]
    Print,
    #[token("list")]
    List,
    #[token("types")]
    Types,
    #[token("type")]
    Type,

    #[regex(r#""([^"\\]|\\.)*""#)]
    String,

    #[regex("[a-zA-Z_][a-zA-Z0-9_]*")]
    Ident,

    #[regex("-?[0-9]+")]
    Number,

    #[token("=")]
    Eq,
    #[token("+")]
    Plus,
    #[token("-")]
    Minus,
    #[token("*")]
    Star,
    #[token("/")]
    Slash,
    #[token("(")]
    LParen,
    #[token(")")]
    RParen,
    #[token(";")]
    Semi,
}

pub type Spanned = (usize, Token, usize);

pub fn lex(input: &str) -> Result<Vec<Spanned>, usize> {
    let mut out = Vec::new();
    let mut lex = LexToken::lexer(input);
    while let Some(tok) = lex.next() {
        let span = lex.span();
        let start = span.start;
        let end = span.end;
        use LexToken::*;
        let t = match tok {
            Comment => continue,
            Newline => Token::Newline,
            Let => Token::Let,
            Print => Token::Print,
            List => Token::List,
            Types => Token::Types,
            Type => Token::Type,
            String => {
                let s = &input[start..end];
                let unquoted = &s[1..s.len() - 1];
                let val = unescape(unquoted);
                Token::StringLit(val)
            }
            Ident => Token::Ident(input[start..end].to_string()),
            Number => {
                let v = input[start..end].parse::<i64>().unwrap();
                Token::Number(v)
            }
            Eq => Token::Eq,
            Plus => Token::Plus,
            Minus => Token::Minus,
            Star => Token::Star,
            Slash => Token::Slash,
            LParen => Token::LParen,
            RParen => Token::RParen,
            Semi => Token::Semi,
        };
        out.push((start, t, end));
    }
    Ok(out)
}

fn unescape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        if c == '\\' {
            if let Some(n) = chars.next() {
                match n {
                    'n' => out.push('\n'),
                    't' => out.push('\t'),
                    'r' => out.push('\r'),
                    '"' => out.push('"'),
                    '\\' => out.push('\\'),
                    other => {
                        out.push('\\');
                        out.push(other);
                    }
                }
            } else {
                out.push('\\');
            }
        } else {
            out.push(c);
        }
    }
    out
}

