#define INTERN_NULL ((InternId){.id = 0})

typedef struct {
    u32 id;
} InternId;

typedef struct {
    Buffer string;
    InternId next;
} InternItem;

// keep this a power of 2 for fast hash modulo
#define INTERN_BUCKET_COUNT 4096
#define INTERN_ITEM_COUNT 8192

// TODO: copy strings to pool owned buffers
typedef struct {
    InternId buckets[INTERN_BUCKET_COUNT];
    InternItem items[INTERN_ITEM_COUNT];
    u32 next_id;
} InternPool;

internal InternPool *
intern_pool_init(Arena *arena);

internal InternId
intern_pool_get_id(InternPool *pool, Buffer string);

internal Buffer
intern_pool_get_string(InternPool *pool, InternId id);
