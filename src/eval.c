typedef enum {
    EvalValue_Void,
    EvalValue_Undef,
    EvalValue_Int,
    EvalValue_Bool,
    EvalValue_Type,
    EvalValue_FnLit,
} EvalValueFormat;

typedef struct {
    TypeId type;
    EvalValueFormat format;
    union {
        i64 integer;
        b32 boolean;
        TypeId type_id;
        // TODO: fn lit
    };
} EvalValue;

typedef enum {
    EvalResult_Pending,
    EvalResult_Poison,
    EvalResult_Known,
    EvalResult_Runtime,
} EvalResultType;

typedef struct {
    EvalResultType type;
    union {
        EvalValue value;
    };
} EvalResult;

typedef enum {
    EvalMode_Exec,
    EvalMode_Emit,
} EvalMode;
