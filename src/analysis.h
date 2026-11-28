#define TIR_NULL ((TirInstId){.id = 0})

typedef struct {
    u32 id;
} TirInstId;

typedef struct {
    u32 id;
} TirExtraId;

typedef enum {
    Tir_FnArg,
    Tir_NumLit,
} TirType;

typedef struct {
    u32 pos;
    TirType type;

    TypeId eval_type;
    union {
        b32 bool_lit;
        u64 num_lit;
    };
} TirInst;

typedef struct {
    TirInst *insts;
    u32 inst_count;

    u32 *extras;
    u32 extra_count;
} Tir;

// decl id pre declared for refs
typedef struct {
    u32 id;
} DeclId;
#define DECL_NULL ((DeclId){.id = 0})

// Name table
typedef struct {
    u32 id;
} NameId;
#define NAME_NULL ((NameId){.id = 0})

typedef struct {
    InternId intern_id;
    DeclId decl;

    NameId next_in_hash;
} Name;

typedef struct Namespace {
    NameId *buckets;
    u32 bucket_count;

    Name *names;
    u32 name_count;
    u32 name_capacity;
} Namespace;

#define NAMESPACE_BUCKET_COUNT 1024
#define NAMESPACE_CAPACITY 2048

internal Namespace *
namespace_init(Arena *arena);

typedef struct {
    b32 redeclared;
    DeclId old_decl;
} AddNameResult;

internal AddNameResult
namespace_add_name(Namespace *ns, InternId intern_id, DeclId decl);

internal DeclId
namespace_find_name(Namespace *ns, InternId intern_id);

// fn table
typedef enum {
    FnStatus_Ready,
    FnStatus_Queued,
    FnStatus_InProgress,
    FnStatus_Done,
} FnStatus;

typedef struct {
    DeclId decl;
    UirInstId uir_inst;

    TypeId type;
    FnStatus status;

    Tir tir;
    TirInstId tir_inst;
} Fn;

typedef struct {
    u32 id;
} FnId;
#define FN_NULL ((FnId){.id = 0})

typedef struct {
    Fn *fns;
    u32 count;
    u32 capacity;
} FnTable;

#define FN_TABLE_CAPACITY 2048

internal FnTable *
fn_table_init(Arena *arena);

internal FnId
fn_table_alloc(FnTable *table, TypeId type);

internal Fn *
fn_table_get_fn(FnTable *table, FnId id);

// eval value
typedef enum {
    EvalValue_Pending,
    EvalValue_Poison,

    EvalValue_NoReturn,
    EvalValue_Void,
    EvalValue_Undef,
    EvalValue_Int,
    EvalValue_Bool,
    EvalValue_Type,
    EvalValue_FnLit,

    EvalValue_Runtime,
} EvalValueFormat;

typedef struct EvalValue {
    EvalValueFormat format;
    TypeId type;
    union {
        i64 integer;
        b32 boolean;
        TypeId type_val;
        FnId fn_lit;
        TirInstId runtime;
    };
} EvalValue;

#define EVAL_VALUE_POISON ((EvalValue){.format = EvalValue_Poison})

#define EVAL_VALUE_NORETURN                                                                       \
    ((EvalValue){.format = EvalValue_NoReturn, .type = TYPE_ID(Type_NoReturn)})

// decl table
typedef enum {
    DeclStatus_Ready,
    DeclStatus_Queued,
    DeclStatus_InProgress,
    DeclStatus_Done,
} DeclStatus;

typedef struct {
    FileId file;
    b32 is_const;

    Uir uir;
    UirInstId uir_inst;

    Namespace *ns;
    EvalValue eval_value;

    DeclStatus status;
} Decl;

typedef struct {
    Decl *decls;
    u32 count;
    u32 capacity;
} DeclTable;

#define DECL_TABLE_CAPACITY 2048

internal DeclTable *
decl_table_init(Arena *arena);

internal DeclId
decl_table_alloc(DeclTable *table, FileId file, Uir uir, struct Namespace *ns, UirInstId uir_inst);

internal Decl *
decl_table_get_decl(DeclTable *table, DeclId id);

// eval state
typedef enum {
    EvalControl_None,
    EvalControl_Break,
} EvalControlType;

typedef struct {
    EvalControlType type;
    EvalValue value;
    UirInstId break_block;
} EvalControl;

typedef struct {
    FileId file;
    Namespace *ns;
    Uir uir;
    EvalValue *values; // length is equal to uir insts

    EvalControl control;
} EvalFrame;

// analysis
typedef enum {
    AnalysisType_Decl,
    AnalysisType_Fn,
} AnalysisType;

typedef struct {
    AnalysisType type;
    union {
        DeclId decl;
        FnId fn;
    };
} AnalysisEntry;

typedef struct {
    AnalysisEntry *entries;
    u32 capacity;

    u32 read_idx;
    u32 queued_count;
} AnalysisQueue;

#define EVAL_MAX_MESSAGE_COUNT 1024
typedef struct {
    Arena scratch;

    TypePool *type_pool;
    InternPool *intern_pool;
    DeclTable *decl_table;
    FnTable *fn_table;

    TirInst *tir_insts;
    u32 inst_count;
    u32 inst_capacity;

    u32 *tir_extras;
    u32 extra_count;
    u32 extra_capacity;

    AnalysisQueue queue;

    Message messages[EVAL_MAX_MESSAGE_COUNT];
    u32 message_count;
} AnalysisState;

#define ANALYSIS_SCRATCH_SIZE (4 * KB)
#define ANALYSIS_TIR_INST_CAPACITY 4096
#define ANALYSIS_TIR_EXTRA_CAPACITY 4096
#define ANALYSIS_QUEUE_CAPACITY 2048

internal AnalysisState *
analysis_state_init(Arena *arena, InternPool *intern_pool);
