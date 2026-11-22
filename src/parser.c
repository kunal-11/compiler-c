#include "lexer.c"
#include "ast.c"

#define MIN_BINDING_POWER 0
#define TYPE_BINDING_POWER 3

internal u32
operator_binding_power(Operator op) {
    u32 result = 0;
    switch (op) {
        case Op_Negate: {
            result = 3;
        } break;

        case Op_Mul:
        case Op_Div:
        case Op_Mod: {
            result = 2;
        } break;

        case Op_Add:
        case Op_Subtract: {
            result = 1;
        } break;

        case Op_Invalid: {
            result = 0;
        } break;
    }
    return result;
}

#define PARSER_LOOKAHEAD 2
#define PARSER_MAX_MESSAGE_COUNT 1024
#define PARSER_SCRATCH_SIZE (64 * KB)

typedef struct {
    Arena scratch;

    FileId file_id;
    Buffer file_contents;
    u32 file_cursor;

    Token token[PARSER_LOOKAHEAD];
    b32 token_filled[PARSER_LOOKAHEAD];

    Ast *ast;
    InternPool *intern_pool;

    Message messages[PARSER_MAX_MESSAGE_COUNT];
    u32 message_count;
} ParserState;

internal ParserState *
parser_init(Arena *arena, FileId file_id, Buffer file_contents, InternPool *intern_pool) {
    ParserState *state = arena_push_struct(arena, ParserState);
    state->file_id = file_id;
    state->file_contents = file_contents;
    state->file_cursor = 0;
    for (u32 i = 0; i < PARSER_LOOKAHEAD; i += 1) {
        state->token_filled[i] = false;
    }
    state->ast = ast_init(arena);
    state->intern_pool = intern_pool;
    state->message_count = 0;

    state->scratch = sub_arena(arena, PARSER_SCRATCH_SIZE);
    return state;
}

internal void
parser_add_message(ParserState *state, Message message) {
    debug_assert(state->message_count < PARSER_MAX_MESSAGE_COUNT, STR_LIT("parser messages OOM"));
    state->messages[state->message_count] = message;
    state->message_count += 1;
}

internal Token
parser_peek_token(ParserState *state, u32 peek_i) {
    debug_assert(peek_i < PARSER_LOOKAHEAD, STR_LIT("parser lookahead out of range"));
    if (!state->token_filled[peek_i]) {
        u32 lex_pos = state->file_cursor;
        if (peek_i > 0) {
            Token prev_token = parser_peek_token(state, peek_i - 1);
            lex_pos = prev_token.end;
        }
        state->token[peek_i] = lexer_lex_token(state->file_contents, lex_pos);
        state->token_filled[peek_i] = true;
    }
    return state->token[peek_i];
}

internal void
parser_advance_token(ParserState *state) {
    Token token = parser_peek_token(state, 0);
    state->file_cursor = token.end;
    for (u32 i = 1; i < PARSER_LOOKAHEAD; i += 1) {
        state->token[i - 1] = state->token[i];
        state->token_filled[i - 1] = state->token_filled[i];
    }
    state->token_filled[PARSER_LOOKAHEAD - 1] = false;
}

internal b32
parser_expect_token(ParserState *state, TokenType type) {
    b32 result = true;
    Token token = parser_peek_token(state, 0);
    if (token.type != type) {
        Message msg = {
            .file = state->file_id,
            .pos = token.begin,
            .severity = Severity_Error,
            .type = Message_UnexpectedToken,
            .expected_token = type,
        };
        parser_add_message(state, msg);
        result = false;
    } else {
        parser_advance_token(state);
    }
    return result;
}

internal void
parser_recover(ParserState *state, b32 consume_end) {
    u32 scope_count = 0;
    b32 consume = true;
    while (consume) {
        switch (parser_peek_token(state, 0).type) {
            case Token_OpenBrace: {
                scope_count += 1;
                parser_advance_token(state);
            } break;

            case Token_CloseBrace: {
                if (scope_count > 0) {
                    scope_count -= 1;
                    parser_advance_token(state);
                } else {
                    consume = false;
                }
            } break;

            case Token_Semicolon: {
                if (scope_count == 0) {
                    consume = false;
                }
            } break;

            case Token_Eof: {
                consume = false;
            } break;

            default: {
                parser_advance_token(state);
            } break;
        }
    }

    if (consume_end) {
        parser_advance_token(state);
    }
}

internal AstNodeId
parser_parse_expr(ParserState *, u32);

internal AstNodeId
parser_parse_fn_type(ParserState *state) {
    AstNodeId result = AST_POISON_ID;
    Flusher flusher = flusher_init(&state->scratch);

    Token fn_key = parser_peek_token(state, 0);
    debug_assert(fn_key.type == Token_KeyFn, STR_LIT("parse fn type invalid token"));
    parser_advance_token(state);

    if (!parser_expect_token(state, Token_OpenParen)) {
        goto cleanup;
    }

    AstNodeId *args = arena_peek_struct(&state->scratch, AstNodeId);
    while (parser_peek_token(state, 0).type == Token_Identifier) {
        Token identifier = parser_peek_token(state, 0);
        parser_advance_token(state);
        Buffer name = buffer_slice(state->file_contents, identifier.begin, identifier.end);

        if (!parser_expect_token(state, Token_Colon)) {
            goto cleanup;
        }

        AstNodeId type = parser_parse_expr(state, TYPE_BINDING_POWER);
        InternId intern_id = intern_pool_get_id(state->intern_pool, name);

        AstNode arg_node = {
            .pos = identifier.begin,
            .type = Ast_FnArg,
            .fn_arg = {.name = intern_id, .type = type},
        };
        *arena_push_struct(&state->scratch, AstNodeId) = ast_alloc_node(state->ast, arg_node);

        if (parser_peek_token(state, 0).type == Token_Comma) {
            parser_advance_token(state);
        } else {
            break;
        }
    }
    if (!parser_expect_token(state, Token_CloseParen)) {
        goto cleanup;
    }

    AstNodeId ret_type = parser_parse_expr(state, TYPE_BINDING_POWER);
    result = ast_alloc_fn_type(state->ast, fn_key.begin, args,
                               (u32)arena_list_count(&state->scratch, args), ret_type);
cleanup:
    flusher_flush(flusher);
    return result;
}

internal AstNodeId
parser_parse_decl(ParserState *state) {
    Token identifier = parser_peek_token(state, 0);
    if (!parser_expect_token(state, Token_Identifier)) {
        return AST_POISON_ID;
    }

    if (!parser_expect_token(state, Token_Colon)) {
        return AST_POISON_ID;
    }

    Token assign = parser_peek_token(state, 0);
    AstNodeId type = AST_NULL_ID;
    if (assign.type != Token_Equals && assign.type != Token_Colon) {
        type = parser_parse_expr(state, TYPE_BINDING_POWER);
    }

    AstNodeType decl_type;
    assign = parser_peek_token(state, 0);
    switch (assign.type) {
        case Token_Equals: {
            decl_type = Ast_VarDecl;
        } break;

        case Token_Colon: {
            decl_type = Ast_ConstDecl;
        } break;

        default: {
            Message msg = {
                .file = state->file_id,
                .pos = state->file_cursor,
                .severity = Severity_Error,
                .type = Message_ExpectedDeclType,
            };
            parser_add_message(state, msg);
            return AST_POISON_ID;
        } break;
    }
    parser_advance_token(state);

    AstNodeId value = parser_parse_expr(state, MIN_BINDING_POWER);

    Buffer name = buffer_slice(state->file_contents, identifier.begin, identifier.end);
    InternId intern_name = intern_pool_get_id(state->intern_pool, name);
    AstNode node = {
        .pos = identifier.begin,
        .type = decl_type,
        .decl = {.name = intern_name, .type = type, .value = value},
    };
    return ast_alloc_node(state->ast, node);
}

internal AstNodeId
parser_parse_statement(ParserState *state) {
    AstNodeId result;
    switch (parser_peek_token(state, 0).type) {
        case Token_Identifier: {
            if (parser_peek_token(state, 1).type == Token_Colon) {
                result = parser_parse_decl(state);
                goto cleanup;
            }
        } break;

        default:
            break;
    }

    result = parser_parse_expr(state, MIN_BINDING_POWER);
    if (parser_peek_token(state, 0).type == Token_Equals) {
        Token token_eq = parser_peek_token(state, 0);
        parser_advance_token(state);

        AstNodeId rhs = parser_parse_expr(state, MIN_BINDING_POWER);
        AstNode assign_node = {
            .pos = token_eq.begin,
            .type = Ast_Assign,
            .assign = {.lhs = result, .rhs = rhs},
        };
        result = ast_alloc_node(state->ast, assign_node);
    }

cleanup:
    if (result.id == AST_POISON_ID.id) {
        parser_recover(state, false);
    }
    parser_expect_token(state, Token_Semicolon);
    return result;
}

internal AstNodeId
parser_parse_fn_type_or_lit(ParserState *state) {
    Token fn_key = parser_peek_token(state, 0);
    AstNodeId type = parser_parse_fn_type(state);
    if (parser_peek_token(state, 0).type == Token_OpenBrace) {
        parser_advance_token(state);
        Flusher flusher = flusher_init(&state->scratch);

        AstNodeId *body = arena_peek_struct(&state->scratch, AstNodeId);
        b32 block_end = false;
        while (!block_end) {
            switch (parser_peek_token(state, 0).type) {
                case Token_CloseBrace:
                case Token_Eof: {
                    block_end = true;
                } break;

                default: {
                    AstNodeId *slot = arena_push_struct(&state->scratch, AstNodeId);
                    *slot = parser_parse_statement(state);
                } break;
            }
        }
        parser_expect_token(state, Token_CloseBrace);
        AstNodeId lit = ast_alloc_fn_lit(state->ast, fn_key.begin, type, body,
                                         (u32)arena_list_count(&state->scratch, body));
        flusher_flush(flusher);
        return lit;
    }
    return type;
}
internal AstNodeId
parser_parse_expr(ParserState *state, u32 parent_binding_pow) {
    AstNodeId result;
    Token token = parser_peek_token(state, 0);
    switch (token.type) {
        case Token_Identifier: {
            parser_advance_token(state);
            Buffer name = buffer_slice(state->file_contents, token.begin, token.end);
            AstNode node = {
                .pos = token.begin,
                .type = Ast_Identifier,
                .identifier = intern_pool_get_id(state->intern_pool, name),
            };
            result = ast_alloc_node(state->ast, node);
        } break;
        case Token_NumLit: {
            parser_advance_token(state);
            AstNode node = {
                .pos = token.begin,
                .type = Ast_NumLit,
            };
            result = ast_alloc_node(state->ast, node);
        } break;
        case Token_KeyFn: {
            result = parser_parse_fn_type_or_lit(state);
        } break;

        // prefix ops
        case Token_Minus: {
            Operator op = Op_Invalid;
            switch (token.type) {
                case Token_Minus: {
                    op = Op_Negate;
                } break;

                default:
                    UNREACHABLE;
            }

            u32 binding_power = operator_binding_power(op);
            if (binding_power <= parent_binding_pow) {
                // ignore the operator with a message
                Message msg = {
                    .file = state->file_id,
                    .pos = token.begin,
                    .severity = Severity_Error,
                    .type = Message_UnexpectedOperator,
                };
                parser_add_message(state, msg);
                parser_advance_token(state);

                result = parser_parse_expr(state, binding_power);
            } else {
                parser_advance_token(state);
                AstNodeId value = parser_parse_expr(state, binding_power);
                AstNode node = {
                    .pos = token.begin,
                    .type = Ast_PrefixOp,
                    .prefix_op = {.op = op, .value = value},
                };
                result = ast_alloc_node(state->ast, node);
            }
        } break;

        default: {
            Message msg = {
                .file = state->file_id,
                .pos = state->file_cursor,
                .severity = Severity_Error,
                .type = Message_ExpectedExpression,
            };
            parser_add_message(state, msg);
            return AST_POISON_ID;
        } break;
    }

    while (true) {
        Token op_token = parser_peek_token(state, 0);
        Operator op;
        switch (op_token.type) {
            case Token_Plus: {
                op = Op_Add;
            } break;

            case Token_Minus: {
                op = Op_Subtract;
            } break;

            default: {
                op = Op_Invalid;
            } break;
        }
        u32 binding_power = operator_binding_power(op);
        if (binding_power <= parent_binding_pow) {
            break;
        }
        parser_advance_token(state);

        // infix ops
        switch (op) {
            case Op_Add:
            case Op_Subtract:
            case Op_Mul:
            case Op_Div:
            case Op_Mod: {
                AstNodeId rhs = parser_parse_expr(state, binding_power);
                AstNode infix_node = {
                    .pos = op_token.begin,
                    .type = Ast_InfixOp,
                    .infix_op = {.op = op, .lhs = result, .rhs = rhs},
                };
                result = ast_alloc_node(state->ast, infix_node);
            } break;

            case Op_Invalid:
            case Op_Negate:
                UNREACHABLE;
        }
    }
    return result;
}

internal AstNodeId
parser_parse_toplevel(ParserState *state) {
    AstNodeId node = parser_parse_decl(state);
    if (node.id == AST_POISON_ID.id) {
        parser_recover(state, true);
    } else {
        parser_expect_token(state, Token_Semicolon);
    }
    return node;
}
