#include "std.h"
#include "intern_pool.h"

#define FILE_NULL ((FileId){.id = 0})
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
    Message_UndeclaredIdentifier,
    Message_MalformedNumLiteral,

    Message_RedeclaredName,
    Message_RuntimeClosure,
    Message_LvalueInvalid,
    Message_LvalueConstant,

    Message_CoersionFailed,
    Message_TypeInRuntime,
    Message_ExpectedType,
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

#include "lexer.h"
#include "parser.h"
#include "uir.h"

#include "type_pool.h"
#include "analysis.h"

#include "debug_printer.h"
