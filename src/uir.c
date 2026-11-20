#define UIR_NULL_ID ((UirInstId){.id = 0})
#define UIR_POISON_ID ((UirInstId){.id = 1})

typedef struct {
    u32 id;
} UirInstId;

typedef enum {
    Uir_CompBlock,
} UirType;

typedef struct {
    u32 pos;
    UirType type;
} UirInst;
