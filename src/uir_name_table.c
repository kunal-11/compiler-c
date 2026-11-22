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

internal void
uir_name_table_init(UirNameTable *table, Arena *arena) {
    table->names = arena_push_array(arena, UirName, UIR_NAME_TABLE_LEN);
    for (u32 i = 0; i < UIR_NAME_TABLE_LEN; i += 1) {
        table->names[i] = (UirName){
            .intern_id = INTERN_NULL_ID,
            .inst_id = UIR_NULL_ID,
            .type = UirNameType_Invalid,
        };
    }
    table->undo_stack = arena_push_array(arena, UirNameUndo, UIR_NAME_TABLE_LEN);
    table->undo_count = 0;
}

internal UirName *
uir_name_table_find_slot(UirNameTable *table, InternId intern_id) {
    u32 hash = intern_id.id % UIR_NAME_TABLE_LEN;
    u32 probe = 0;
    while (probe < UIR_NAME_TABLE_MAX_PROBES) {
        UirName name = table->names[(hash + probe) % UIR_NAME_TABLE_LEN];
        if (name.intern_id.id == INTERN_NULL_ID.id || name.intern_id.id == intern_id.id) {
            break;
        }
        probe += 1;
    }

    debug_assert(probe != UIR_NAME_TABLE_MAX_PROBES, STR_LIT("uir name table probes OOB"));
    return &table->names[(hash + probe) % UIR_NAME_TABLE_LEN];
}

typedef struct {
    b32 redeclared;
    UirName old_name;
} UirNameAddResult;

internal UirNameAddResult
uir_name_table_add_name(UirNameTable *table, UirName name) {
    UirName *slot = uir_name_table_find_slot(table, name.intern_id);

    // shadowing not allowed on same lexical depth
    if (slot->intern_id.id != INTERN_NULL_ID.id && slot->lexical_depth == name.lexical_depth) {
        return (UirNameAddResult){.redeclared = true, .old_name = *slot};
    }

    debug_assert(table->undo_count < UIR_NAME_TABLE_LEN, STR_LIT("uir name table undo OOM"));
    table->undo_stack[table->undo_count] = (UirNameUndo){
        .intern_id = name.intern_id,
        .old_name = *slot,
    };
    table->undo_count += 1;

    *slot = name;
    return (UirNameAddResult){.redeclared = false};
}

typedef enum {
    UirFindName_Empty,
    UirFindName_Closure,
    UirFindName_Success,
} UirNameFindTag;

typedef struct {
    UirNameFindTag tag;
    UirName name;
} UirNameFindResult;

internal UirNameFindResult
uir_name_table_find_name(UirNameTable *table, InternId intern_id, u32 fn_depth) {
    UirName *slot = uir_name_table_find_slot(table, intern_id);
    if (slot->intern_id.id == INTERN_NULL_ID.id) {
        return (UirNameFindResult){.tag = UirFindName_Empty};
    }
    if (slot->fn_depth != fn_depth && slot->type != UirNameType_Const) {
        return (UirNameFindResult){.tag = UirFindName_Closure, .name = *slot};
    }
    return (UirNameFindResult){.tag = UirFindName_Success, .name = *slot};
}

internal void
uir_name_table_undo(UirNameTable *table, u32 undo_marker) {
    debug_assert(undo_marker <= table->undo_count, STR_LIT("uir name table undo marker OOB"));
    while (table->undo_count > undo_marker) {
        UirNameUndo undo = table->undo_stack[table->undo_count - 1];
        UirName *slot = uir_name_table_find_slot(table, undo.intern_id);
        *slot = undo.old_name;
        table->undo_count -= 1;
    }
}
