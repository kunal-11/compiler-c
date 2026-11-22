#include "std.c"
#include "intern_pool.c"

typedef enum {
    Op_Invalid,

    Op_Add,
    Op_Subtract,

    Op_Mul,
    Op_Div,
    Op_Mod,

    Op_Negate,
} Operator;

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
    Message_ExpectedAddressable,

    Message_RedeclaredName,
    Message_RuntimeClosure,
    Message_LvalueInvalid,
    Message_LvalueConstant,
} MessageType;

typedef struct {
    MessageSeverity severity;
    MessageType type;
    union {
        u32 expected_token;
        u32 original_declaration;
    };

    FileId file;
    u32 pos;
} Message;

#include "parser.c"
#include "uir_lower.c"
