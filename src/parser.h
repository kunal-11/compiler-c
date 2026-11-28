#define AST_NULL ((AstNodeId){.id = 0})
#define AST_POISON ((AstNodeId){.id = 1})

typedef struct {
    u32 id;
} AstNodeId;

typedef struct {
    u32 id;
} AstExtraId;

typedef struct {
    AstExtraId first;
    u32 count;
} AstNodeList;

typedef struct {
    AstNodeList args;
    AstNodeId ret_type;
} AstFnType;

typedef struct {
    InternId name;
    AstNodeId type;
} AstFnArg;

typedef struct {
    AstNodeId type;
    AstNodeList body;
} AstFnLit;

// type can be null for inferred types
typedef struct {
    InternId name;
    AstNodeId type;
    AstNodeId value;
} AstDecl;

typedef enum {
    Op_Invalid,

    Op_Add,
    Op_Subtract,

    Op_Mul,
    Op_Div,
    Op_Mod,

    Op_Negate,
} Operator;

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
        // AstList block;

        AstDecl decl;
        InternId identifier;

        AstFnType fn_type;
        AstFnArg fn_arg;

        AstPrefixOp prefix_op;
        AstInfixOp infix_op;
        AstAssign assign;

        AstFnLit fn_lit;
        b32 bool_lit;
        u64 num_lit;
    };
} AstNode;

typedef struct {
    AstNode *nodes;
    u32 node_count;
    u32 node_capacity;

    u32 *extras;
    u32 extra_count;
    u32 extra_capacity;
} Ast;

internal void
ast_flush(Ast *ast);

internal AstNodeId
ast_get_list_node(Ast *ast, AstNodeList list, u32 idx);

internal AstNode
ast_get_node(Ast *ast, AstNodeId id);

#define PARSER_NODE_CAPACITY 8192
#define PARSER_EXTRA_CAPACITY 8192

#define PARSER_LOOKAHEAD 2
#define PARSER_MAX_MESSAGE_COUNT 1024
#define PARSER_SCRATCH_SIZE (64 * KB)

typedef struct {
    Arena scratch;
    InternPool *intern_pool;

    FileId file_id;
    Buffer file_contents;
    u32 file_cursor;

    Token token[PARSER_LOOKAHEAD];
    b32 token_filled[PARSER_LOOKAHEAD];

    Ast ast;

    Message messages[PARSER_MAX_MESSAGE_COUNT];
    u32 message_count;
} ParserState;

internal ParserState *
parser_init(Arena *arena, FileId file_id, Buffer file_contents, InternPool *intern_pool);

internal AstNodeId
parser_parse_toplevel(ParserState *state);
