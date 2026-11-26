#define AST_NULL ((AstNodeId){.id = 0})
#define AST_POISON ((AstNodeId){.id = 1})

typedef struct {
    u32 id;
} AstNodeId;

typedef struct {
    u32 id;
} AstExtraId;

// extra offset - list of FnArg node ids
typedef struct {
    AstExtraId first_arg;
    u32 arg_count;

    AstNodeId ret_type;
} AstFnType;

typedef struct {
    InternId name;
    AstNodeId type;
} AstFnArg;

typedef struct {
    AstExtraId first;
    u32 count;
} AstBlock;

typedef struct {
    AstNodeId type;
    AstBlock body;
} AstFnLit;

// type can be null for inferred types
typedef struct {
    InternId name;
    AstNodeId type;
    AstNodeId value;
} AstDecl;

typedef struct {
    Operator op;
    AstNodeId lhs;
    AstNodeId rhs;
} AstInfixOp;

typedef struct {
    AstNodeId lhs;
    AstNodeId rhs;
} AstAssign;

typedef struct {
    Operator op;
    AstNodeId value;
} AstPrefixOp;

typedef enum {
    Ast_ConstDecl,
    Ast_VarDecl,

    Ast_Identifier,
    Ast_FnType,
    Ast_FnArg,

    Ast_PrefixOp,
    Ast_InfixOp,
    Ast_Assign,

    Ast_FnLit,
    Ast_NumLit,
    Ast_UndefLit,
    Ast_BoolLit,
} AstNodeType;

typedef struct {
    u32 pos;
    AstNodeType type;
    union {
        AstBlock block;

        AstDecl decl;
        InternId identifier;

        AstFnType fn_type;
        AstFnArg fn_arg;

        AstPrefixOp prefix_op;
        AstInfixOp infix_op;
        AstAssign assign;

        AstFnLit fn_lit;
        b32 bool_lit;
    };
} AstNode;

typedef struct {
    AstNode *nodes;
    u32 next_node;
    u32 node_count;

    u32 *extras;
    u32 next_extra;
    u32 extra_count;
} Ast;

// TODO: maybe move to dynamic sizes
#define AST_NODE_COUNT 8192
#define AST_EXTRA_COUNT 8192

internal Ast *
ast_init(Arena *arena) {
    Ast *result = arena_push_struct(arena, Ast);
    result->nodes = arena_push_array(arena, AstNode, AST_NODE_COUNT);
    result->node_count = AST_NODE_COUNT;
    result->next_node = 2;

    result->extras = arena_push_array(arena, u32, AST_EXTRA_COUNT);
    result->next_extra = 0;
    result->extra_count = AST_EXTRA_COUNT;
    return result;
}

internal AstNodeId
ast_alloc_node(Ast *ast, AstNode node) {
    debug_assert(ast->next_node < ast->node_count, STR_LIT("ast nodes OOM"));
    AstNodeId result = {.id = ast->next_node};
    ast->next_node += 1;
    ast->nodes[result.id] = node;
    return result;
}

internal AstNodeId
ast_alloc_fn_type(Ast *ast, u32 pos, AstNodeId *args, u32 args_len, AstNodeId ret_type) {
    debug_assert(ast->next_extra + args_len <= ast->extra_count, STR_LIT("ast extras OOM"));
    AstExtraId offset = {.id = ast->next_extra};
    for (u32 i = 0; i < args_len; i += 1) {
        ast->extras[ast->next_extra + i] = args[i].id;
    }
    ast->next_extra += args_len;
    AstNode node = {
        .pos = pos,
        .type = Ast_FnType,
        .fn_type = {.first_arg = offset, .arg_count = args_len, .ret_type = ret_type},
    };
    return ast_alloc_node(ast, node);
}

internal AstNodeId
ast_get_fn_arg_id(Ast *ast, AstFnType fn_type, u32 idx) {
    debug_assert(fn_type.first_arg.id + fn_type.arg_count <= ast->next_extra,
                 STR_LIT("ast fn arg extra offset OOB"));
    debug_assert(idx < fn_type.arg_count, STR_LIT("fn arg idx OOB"));
    return (AstNodeId){.id = ast->extras[fn_type.first_arg.id + idx]};
}

internal AstNodeId
ast_alloc_fn_lit(Ast *ast, u32 pos, AstNodeId type, AstNodeId *body, u32 body_len) {
    debug_assert(ast->next_extra + body_len <= ast->extra_count, STR_LIT("ast extras OOM"));
    AstExtraId offset = {.id = ast->next_extra};
    for (u32 i = 0; i < body_len; i += 1) {
        ast->extras[ast->next_extra + i] = body[i].id;
    }
    ast->next_extra += body_len;
    AstNode node = {
        .pos = pos,
        .type = Ast_FnLit,
        .fn_lit = {.type = type, .body = {.first = offset, .count = body_len}},
    };
    return ast_alloc_node(ast, node);
}

internal AstNodeId
ast_get_block_node(Ast *ast, AstBlock block, u32 idx) {
    debug_assert(block.first.id + block.count <= ast->next_extra, STR_LIT("ast extra offset OOB"));
    debug_assert(idx < block.count, STR_LIT("block idx OOB"));
    return (AstNodeId){.id = ast->extras[block.first.id + idx]};
}

internal AstNode
ast_get_node(Ast *ast, AstNodeId id) {
    debug_assert(id.id != AST_NULL.id, STR_LIT("ast get null node"));
    debug_assert(id.id != AST_POISON.id, STR_LIT("ast get poison node"));
    debug_assert(id.id < ast->next_node, STR_LIT("ast node OOB"));
    return ast->nodes[id.id];
}
