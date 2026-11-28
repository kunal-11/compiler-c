typedef enum {
    Token_Invalid,
    Token_Eof,

    Token_NumLit,

    Token_Identifier,

    Token_KeyFn,

    Token_OpenParen,
    Token_CloseParen,

    Token_OpenBrace,
    Token_CloseBrace,

    Token_Colon,
    Token_Semicolon,

    Token_Arrow,
    Token_Comma,
    Token_Equals,

    Token_Plus,
    Token_Minus,
    Token_Mul,
    Token_Div,
    Token_Mod,
} TokenType;

typedef struct {
    TokenType type;
    u32 begin;
    u32 end;
} Token;

internal Token
lexer_lex_token(Buffer contents, u32 pos);
