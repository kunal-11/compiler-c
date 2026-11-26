typedef enum {
    DeclStatus_Ready,
    DeclStatus_Queued,
    DeclStatus_InProgress,
    DeclStatus_Done,
} DeclStatus;

typedef struct {
    FileId file;

    Uir *uir;
    UirInstId uir_inst;

    DeclStatus status;
} Decl;

#define DECL_NULL ((DeclId){.id = 0})
typedef struct {
    u32 id;
} DeclId;

typedef struct {
    Decl *decls;
    u32 count;
    u32 capacity;
} DeclTable;

#define DECL_TABLE_CAPACITY 2048

internal DeclTable *
decl_table_init(Arena *arena) {
    DeclTable *table = arena_push_struct(arena, DeclTable);
    table->decls = arena_push_array(arena, Decl, DECL_TABLE_CAPACITY);
    table->capacity = DECL_TABLE_CAPACITY;
    table->count = 1;
    return table;
}

internal DeclId
decl_table_alloc(DeclTable *table, FileId file, Uir *uir, UirInstId uir_inst) {
    debug_assert(table->count < table->capacity, STR_LIT("decl table OOM"));
    DeclId result = {.id = table->count};
    table->count += 1;
    table->decls[result.id] = (Decl){
        .file = file,
        .uir = uir,
        .uir_inst = uir_inst,
        .status = DeclStatus_Ready,
    };
    return result;
}

internal Decl *
decl_table_get_decl(DeclTable *table, DeclId id) {
    debug_assert(id.id != DECL_NULL.id, STR_LIT("decl table get null id"));
    debug_assert(id.id < table->count, STR_LIT("decl table id OOB"));
    return table->decls + id.id;
}

typedef struct {
    u32 id;
} NameId;
#define NAME_NULL ((NameId){.id = 0})

typedef struct {
    InternId intern_id;
    DeclId decl;

    NameId next_in_hash;
} Name;

typedef struct {
    NameId *buckets;
    u32 bucket_count;

    Name *names;
    u32 name_count;
    u32 name_capacity;
} Namespace;

#define NAMESPACE_BUCKET_COUNT 1024
#define NAMESPACE_CAPACITY 2048

internal Namespace *
namespace_init(Arena *arena) {
    Namespace *ns = arena_push_struct(arena, Namespace);
    ns->buckets = arena_push_array(arena, NameId, NAMESPACE_BUCKET_COUNT);
    ns->bucket_count = NAMESPACE_BUCKET_COUNT;
    for (u32 i = 0; i < NAMESPACE_BUCKET_COUNT; i += 1) {
        ns->buckets[i] = NAME_NULL;
    }

    ns->names = arena_push_array(arena, Name, NAMESPACE_CAPACITY);
    ns->name_count = 1;
    ns->name_capacity = NAMESPACE_CAPACITY;

    return ns;
}

internal Name
namespace_get_name(Namespace *ns, NameId id) {
    debug_assert(id.id < ns->name_count, STR_LIT("name id OOB"));
    return ns->names[id.id];
}

internal NameId *
namespace_get_slot(Namespace *ns, InternId id) {
    debug_assert(id.id != INTERN_NULL.id, STR_LIT("ns get null slot"));
    NameId *slot = &ns->buckets[id.id % ns->bucket_count];
    while (slot->id != NAME_NULL.id) {
        if (namespace_get_name(ns, *slot).intern_id.id == id.id) {
            break;
        }
        slot = &ns->names[slot->id].next_in_hash;
    }
    return slot;
}

typedef struct {
    b32 redeclared;
    DeclId old_decl;
} AddNameResult;

internal AddNameResult
namespace_add_name(Namespace *ns, InternId intern_id, DeclId decl) {
    NameId *slot = namespace_get_slot(ns, intern_id);
    AddNameResult res = {.redeclared = false};
    if (slot->id != NAME_NULL.id) {
        res = (AddNameResult){
            .redeclared = true,
            .old_decl = namespace_get_name(ns, *slot).decl,
        };
    } else {
        debug_assert(ns->name_count < ns->name_capacity, STR_LIT("namespace OOM"));
        slot->id = ns->name_count;
        ns->names[ns->name_count] = (Name){
            .intern_id = intern_id,
            .decl = decl,
            .next_in_hash = NAME_NULL,
        };
        ns->name_count += 1;
    }
    return res;
}

internal DeclId
namespace_find_name(Namespace *ns, InternId intern_id) {
    NameId *slot = namespace_get_slot(ns, intern_id);
    if (slot->id == NAME_NULL.id) {
        return DECL_NULL;
    }
    return namespace_get_name(ns, *slot).decl;
}
