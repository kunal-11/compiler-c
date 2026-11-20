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
} TokenType;

typedef struct {
    TokenType type;
    u32 begin;
    u32 end;
} Token;

internal Token
lexer_fixed_token(TokenType type, u32 at, u32 len) {
    return (Token){.type = type, .begin = at, .end = at + len};
}

internal Token
lexer_parse_word(Buffer contents, u32 begin) {
    u32 end = begin;
    while (end < contents.len) {
        u8 byte = contents.bytes[end];
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '_') {
        } else {
            break;
        }
        end += 1;
    }
    Buffer word = buffer_slice(contents, begin, end);
    if (buffer_eql(word, STR_LIT("fn"))) {
        return (Token){.type = Token_KeyFn, .begin = begin, .end = end};
    } else {
        return (Token){.type = Token_Identifier, .begin = begin, .end = end};
    }
}

internal Token
lexer_parse_numlit(Buffer contents, u32 begin) {
    u32 end = begin;
    while (end < contents.len) {
        u8 byte = contents.bytes[end];
        if ((byte >= '0' && byte <= '9') || byte == '_') {
        } else {
            break;
        }
        end += 1;
    }
    return (Token){.type = Token_NumLit, .begin = begin, .end = end};
}

internal b32
lexer_is_whitespace(u8 byte) {
    return byte == ' ' || byte == '\n' || byte == '\r' || byte == '\t';
}

internal Token
lexer_parse_invalid(Buffer contents, u32 begin) {
    u32 end = begin;
    while (end < contents.len) {
        u8 byte = contents.bytes[end];
        if (lexer_is_whitespace(byte) || byte == ')' || byte == '}' || byte == ';') {
            break;
        }
        end += 1;
    }
    return (Token){.type = Token_Invalid, .begin = begin, .end = end};
}

internal Token
lexer_lex_token(Buffer contents, u32 pos) {
    u32 i = pos;
    while (i < contents.len) {
        u8 byte = contents.bytes[i];
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_') {
            return lexer_parse_word(contents, i);
        } else if (byte >= '0' && byte <= '9') {
            return lexer_parse_numlit(contents, i);
        } else if (byte == ';') {
            return lexer_fixed_token(Token_Semicolon, i, 1);
        } else if (byte == ',') {
            return lexer_fixed_token(Token_Comma, i, 1);
        } else if (byte == ':') {
            return lexer_fixed_token(Token_Colon, i, 1);
        } else if (byte == '=') {
            return lexer_fixed_token(Token_Equals, i, 1);
        } else if (byte == '+') {
            return lexer_fixed_token(Token_Plus, i, 1);
        } else if (byte == '-') {
            if (i + 1 < contents.len && contents.bytes[i + 1] == '>') {
                return lexer_fixed_token(Token_Arrow, i, 2);
            } else {
                return lexer_fixed_token(Token_Minus, i, 1);
            }
        } else if (byte == '{') {
            return lexer_fixed_token(Token_OpenBrace, i, 1);
        } else if (byte == '}') {
            return lexer_fixed_token(Token_CloseBrace, i, 1);
        } else if (byte == '(') {
            return lexer_fixed_token(Token_OpenParen, i, 1);
        } else if (byte == ')') {
            return lexer_fixed_token(Token_CloseParen, i, 1);
        } else if (lexer_is_whitespace(byte)) {
        } else {
            return lexer_parse_invalid(contents, i);
        }
        i += 1;
    }
    return (Token){.type = Token_Eof, .begin = (u32)contents.len, .end = (u32)contents.len};
}
