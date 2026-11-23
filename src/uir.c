#define UIR_NULL_ID ((UirInstId){.id = 0})
#define UIR_POISON_ID ((UirInstId){.id = 1})

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

    Uir_NumLit,
    Uir_UndefLit,
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
        UirBreak break_block;

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

        UirInstId ret;
    };
} UirInst;

typedef struct {
    UirInst *insts;
    u32 next_inst;
    u32 inst_count;

    u32 *extras;
    u32 next_extra;
    u32 extra_count;
} Uir;

#define UIR_INST_COUNT 8192
#define UIR_EXTRA_COUNT 8192

internal Uir *
uir_init(Arena *arena) {
    Uir *result = arena_push_struct(arena, Uir);
    result->insts = arena_push_array(arena, UirInst, UIR_INST_COUNT);
    result->next_inst = 2;
    result->inst_count = UIR_INST_COUNT;

    result->extras = arena_push_array(arena, u32, UIR_EXTRA_COUNT);
    result->next_extra = 0;
    result->extra_count = UIR_EXTRA_COUNT;
    return result;
}

internal UirInstId
uir_alloc_inst(Uir *uir, UirInst inst) {
    debug_assert(uir->next_inst < uir->inst_count, STR_LIT("uir OOM"));
    UirInstId result = {.id = uir->next_inst};
    uir->next_inst += 1;
    uir->insts[result.id] = inst;
    return result;
}

internal void
uir_overwrite_inst(Uir *uir, UirInstId reserved_inst, UirInst inst) {
    debug_assert(reserved_inst.id > 1, STR_LIT("uir overwrite null or poison inst"));
    debug_assert(reserved_inst.id < uir->next_inst, STR_LIT("uir overwrite OOB"));
    uir->insts[reserved_inst.id] = inst;
}

internal UirInst
uir_get_inst(Uir *uir, UirInstId inst_id) {
    debug_assert(inst_id.id != UIR_NULL_ID.id, STR_LIT("uir get null inst"));
    debug_assert(inst_id.id != UIR_POISON_ID.id, STR_LIT("uir get poison inst"));
    debug_assert(inst_id.id < uir->next_inst, STR_LIT("uir inst id OOB"));
    return uir->insts[inst_id.id];
}

// functions for extra access
internal UirBlock
uir_alloc_block(Uir *uir, u32 count) {
    debug_assert(uir->next_extra + count <= uir->extra_count, STR_LIT("uir extra OOM"));
    UirExtraId offset = {.id = uir->next_extra};
    uir->next_extra += count;
    return (UirBlock){.first = offset, .count = count};
}

internal UirInstId
uir_get_block_inst_id(Uir *uir, UirBlock block, u32 idx) {
    debug_assert(block.first.id + block.count <= uir->next_extra, STR_LIT("invalid uir block"));
    debug_assert(idx < block.count, STR_LIT("block idx OOB"));
    return (UirInstId){.id = uir->extras[block.first.id + idx]};
}

internal void
uir_write_block_inst(Uir *uir, UirBlock block, u32 idx, UirInstId inst_id) {
    debug_assert(block.first.id + block.count <= uir->next_extra, STR_LIT("invalid uir block"));
    debug_assert(idx < block.count, STR_LIT("block idx OOB"));
    uir->extras[block.first.id + idx] = inst_id.id;
}

internal Buffer uir_fmt_indent = STR_LIT("    ");

internal void
uir_print(Uir *uir, InternPool *intern_pool, UirInstId inst_id, u32 indent) {
    debug_assert(inst_id.id != UIR_NULL_ID.id, STR_LIT("uir null inst print"));
    for (u32 i = 0; i < indent; i += 1) {
        printf("%.*s", (int)uir_fmt_indent.len, uir_fmt_indent.bytes);
    }
    if (inst_id.id == UIR_POISON_ID.id) {
        printf("POISON\n");
        return;
    }

    printf("%%%d = ", inst_id.id);
    UirInst inst = uir_get_inst(uir, inst_id);
    switch (inst.type) {
        case Uir_CompBlock: {
            printf("comp_block {\n");
            for (u32 i = 0; i < inst.block.count; i += 1) {
                UirInstId child = uir_get_block_inst_id(uir, inst.block, i);
                uir_print(uir, intern_pool, child, indent + 1);
            }
            for (u32 i = 0; i < indent; i += 1) {
                printf("%.*s", (int)uir_fmt_indent.len, uir_fmt_indent.bytes);
            }
            printf("}\n");
        } break;

        case Uir_FnLit: {
            printf("fn_lit %%%d {\n", inst.fn_lit.fn_type.id);
            for (u32 i = 0; i < inst.fn_lit.body.count; i += 1) {
                UirInstId child = uir_get_block_inst_id(uir, inst.fn_lit.body, i);
                uir_print(uir, intern_pool, child, indent + 1);
            }
            for (u32 i = 0; i < indent; i += 1) {
                printf("%.*s", (int)uir_fmt_indent.len, uir_fmt_indent.bytes);
            }
            printf("}\n");
        } break;

        case Uir_FnArg: {
            printf("fn_arg #%d\n", inst.fn_arg_idx);
        } break;

        case Uir_FnType: {
            printf("fn_type ( ");
            for (u32 i = 0; i < inst.fn_type.args.count; i += 1) {
                UirInstId arg_id = uir_get_block_inst_id(uir, inst.fn_type.args, i);
                printf("%%%d ", arg_id.id);
            }
            printf(") %%%d\n", inst.fn_type.ret_type.id);
        } break;

        case Uir_NumLit: {
            printf("num_lit\n");
        } break;

        case Uir_BoolLit: {
            printf("bool_lit %d\n", inst.bool_lit);
        } break;

        case Uir_UndefLit: {
            printf("undef_lit\n");
        } break;

        case Uir_Coerce: {
            printf("coerce (%%%d) %%%d\n", inst.coerce.type.id, inst.coerce.value.id);
        } break;

        case Uir_TypeOf: {
            printf("type_of %%%d\n", inst.type_of.id);
        } break;

        case Uir_PrefixOp: {
            printf("prefix_op ");
            print_operator(inst.prefix_op.op);
            printf(" %%%d\n", inst.prefix_op.value.id);
        } break;

        case Uir_InfixOp: {
            printf("infix_op %%%d ", inst.infix_op.lhs.id);
            print_operator(inst.infix_op.op);
            printf(" %%%d\n", inst.infix_op.rhs.id);
        } break;

        case Uir_GlobalValue: {
            Buffer name = intern_pool_get_string(intern_pool, inst.global);
            printf("global_value #%.*s\n", (int)name.len, name.bytes);
        } break;

        case Uir_GlobalPointer: {
            Buffer name = intern_pool_get_string(intern_pool, inst.global);
            printf("global_ptr #%.*s\n", (int)name.len, name.bytes);
        } break;

        case Uir_Alloc: {
            printf("alloc %%%d\n", inst.alloc_type.id);
        } break;

        case Uir_Load: {
            printf("load %%%d\n", inst.load.id);
        } break;

        case Uir_Store: {
            printf("store %%%d %%%d\n", inst.store.lhs.id, inst.store.rhs.id);
        } break;

        case Uir_Break: {
            printf("break :%%%d %%%d\n", inst.break_block.block_id.id, inst.break_block.value.id);
        } break;

        case Uir_BreakImplicit: {
            printf("break_implicit :%%%d\n", inst.break_block.block_id.id);
        } break;

        case Uir_Ret: {
            printf("return %%%d\n", inst.ret.id);
        } break;

        case Uir_RetImplicit: {
            printf("return_implicit\n");
        } break;

        case Uir_Null:
            UNREACHABLE;
            break;
    }
}
