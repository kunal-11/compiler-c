#include "std.c"

#define FILE_NULL_ID ((FileId){.id = 0})
typedef struct {
    u32 id;
} FileId;

typedef struct {
    Buffer path;
    Buffer contents;
} File;

typedef enum {
    Severity_Info,
    Severity_Warn,
    Severity_Error,
} MessageSeverity;

typedef enum {
    Message_UnexpectedToken,
    Message_UnexpectedOperator,
    Message_ExpectedDeclType,
    Message_ExpectedExpression,
} MessageType;

typedef struct {
    MessageSeverity severity;
    MessageType type;
    union {
        u32 expected_token;
    };

    FileId file;
    u32 pos;
} Message;

typedef enum {
    Op_Invalid,

    Op_Add,
    Op_Subtract,

    Op_Mul,
    Op_Div,
    Op_Mod,

    Op_Negate,
} Operator;

#define MIN_BINDING_POWER 0
#define TYPE_BINDING_POWER 1

internal u32
operator_binding_power(Operator op) {
    u32 result = 0;
    switch (op) {
        case Op_Negate: {
            result = 4;
        } break;

        case Op_Mul:
        case Op_Div:
        case Op_Mod: {
            result = 3;
        } break;

        case Op_Add:
        case Op_Subtract: {
            result = 2;
        } break;

        case Op_Invalid: {
            result = 0;
        } break;
    }
    return result;
}

#include "intern_pool.c"
#include "parser.c"
