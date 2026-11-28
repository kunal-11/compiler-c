#define UIR_NULL ((UirInstId){.id = 0})
#define UIR_POISON ((UirInstId){.id = 1})

typedef struct {
    u32 id;
} UirInstId;

typedef struct {
    u32 id;
} UirExtraId;

typedef enum {
    Uir_Null, // used for reserving and overwrite inst

    Uir_CompBlock,
    Uir_FnLit,
    Uir_FnArg,
    Uir_FnType,

    Uir_VoidLit,
    Uir_UndefLit,
    Uir_NumLit,
    Uir_BoolLit,

    Uir_Coerce,
    Uir_TypeOf,

    Uir_PrefixOp,
    Uir_InfixOp,

    Uir_GlobalValue,
    Uir_GlobalPointer,

    Uir_Alloc,
    Uir_Load,
    Uir_Store,

    Uir_Break,
    Uir_BreakImplicit,

    Uir_Ret,
    Uir_RetImplicit,
} UirType;

typedef struct {
    UirInstId type;
    UirInstId value;
} UirCoerce;

typedef struct {
    UirInstId block_id;
    UirInstId value;
} UirBreak;

typedef struct {
    UirExtraId first;
    u32 count;
} UirBlock;

typedef struct {
    UirInstId fn_type;
    UirBlock body;
} UirFnLit;

// each arg stores a type inst id in extras
typedef struct {
    UirInstId ret_type;
    UirBlock args;
} UirFnType;

typedef struct {
    Operator op;
    UirInstId value;
} UirPrefixOp;

typedef struct {
    Operator op;
    UirInstId lhs;
    UirInstId rhs;
} UirInfixOp;

typedef struct {
    UirInstId lhs;
    UirInstId rhs;
} UirStore;

typedef struct {
    u32 pos;
    UirType type;
    union {
        b32 bool_lit;
        u64 num_lit;

        UirBreak break_block;
        UirInstId ret;

        UirCoerce coerce;
        UirInstId type_of;

        UirBlock block;
        UirFnLit fn_lit;

        UirFnType fn_type;
        u32 fn_arg_idx;

        UirPrefixOp prefix_op;
        UirInfixOp infix_op;

        InternId global;
        UirStore store;
        UirInstId load;
        UirInstId alloc_type;
    };
} UirInst;

typedef struct {
    UirInst *insts;
    u32 inst_count;

    u32 *extras;
    u32 extra_count;
} Uir;

internal UirInst
uir_get_inst(Uir *uir, UirInstId inst_id);

internal UirInstId
uir_get_block_inst_id(Uir *uir, UirBlock block, u32 idx);

typedef enum {
    UirNameType_Invalid,

    UirNameType_Arg,
    UirNameType_Const,
    UirNameType_Var,
} UirNameType;

typedef struct {
    InternId intern_id;
    UirInstId inst_id;
    UirNameType type;

    u32 pos;
    u32 lexical_depth;
    u32 fn_depth;
} UirName;

typedef struct {
    InternId intern_id;
    UirName old_name;
} UirNameUndo;

// open addressed hash table with an undo list
// deletion happens in LIFO order so tombstones are not needed
typedef struct {
    UirName *names;

    UirNameUndo *undo_stack;
    u32 undo_count;
} UirNameTable;

#define UIR_NAME_TABLE_LEN 8192
#define UIR_NAME_TABLE_MAX_PROBES (UIR_NAME_TABLE_LEN / 4)

typedef enum {
    Block_Comptime,
    Block_FnLit,
} BlockType;

typedef struct {
    BlockType type;

    UirInstId *insts;
    Flusher flusher;

    u32 name_undo_marker;

    // NULL incase of nested Block_Comptime
    UirInstId inst_id;

    // below fields are only valid on a Block_FnLit
    u32 prev_comp_nests;
    UirInstId fn_lit_type;
} Block;

#define UIR_MAX_BLOCK_DEPTH 2048
#define UIR_SCRATCH_SIZE (1 * MB)

#define UIR_MAX_MESSAGE_COUNT 1024

#define UIR_INST_CAPACITY 8192
#define UIR_EXTRA_CAPACITY 8192
typedef struct {

    Arena scratch;
    u32 comp_nests;

    Block blocks[UIR_MAX_BLOCK_DEPTH];
    u32 block_count;

    UirNameTable name_table;
    u32 fn_depth;

    UirInst *insts;
    u32 inst_count;
    u32 inst_capacity;

    u32 *extras;
    u32 extra_count;
    u32 extra_capacity;

    Message messages[UIR_MAX_MESSAGE_COUNT];
    u32 message_count;

    // initialized per lower
    Ast *ast;
    FileId file_id;
} UirLowerState;

internal UirLowerState *
uir_lower_init(Arena *arena);

internal UirInstId
uir_lower_global(UirLowerState *state, FileId file_id, Ast *ast, AstNodeId node_id);

internal Uir
uir_lower_emit_chunk(UirLowerState *state);
