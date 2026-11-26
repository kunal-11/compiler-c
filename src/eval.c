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

typedef struct {
    EvalValueFormat format;
    TypeId type;
    union {
        i64 integer;
        b32 boolean;
        TypeId type_val;
        FnId fn_lit;
        // TODO: runtime inst
    };
} EvalValue;

#define EVAL_VALUE_POISON ((EvalValue){.format = EvalValue_Poison})

internal EvalValue
eval_value_type(TypeId type) {
    debug_assert(type.id != Type_None, STR_LIT("eval invalid type value"));
    return (EvalValue){
        .format = EvalValue_Type,
        .type = TYPE_ID(Type_TypeId),
        .type_val = type,
    };
}

#define EVAL_VALUE_NORETURN                                                                       \
    ((EvalValue){.format = EvalValue_NoReturn, .type = TYPE_ID(Type_NoReturn)})

typedef enum {
    EvalControl_None,
    EvalControl_Break,
} EvalControlType;

typedef struct {
    EvalControlType type;
    EvalValue value;
    UirInstId break_block;
} EvalControl;

#define EVAL_MAX_MESSAGE_COUNT 1024
typedef struct {
    FileId file;
    Arena scratch;

    TypePool *type_pool;
    InternPool *intern_pool;
    DeclTable *decl_table;
    FnTable *fn_table;

    Namespace *ns;
    Uir *uir;
    EvalValue *values; // length is equal to uir insts

    EvalControl control;

    Message messages[EVAL_MAX_MESSAGE_COUNT];
    u32 message_count;
} EvalState;

#define EVAL_SCRATCH_SIZE (4 * KB)

internal EvalState *
eval_state_init(Arena *arena, InternPool *intern_pool, TypePool *type_pool, DeclTable *decl_table,
                FnTable *fn_table, Namespace *ns, Uir *uir, FileId file_id) {
    EvalState *state = arena_push_struct(arena, EvalState);
    state->uir = uir;
    state->values = arena_push_array(arena, EvalValue, uir->next_inst);
    for (u32 i = 0; i < uir->next_inst; i += 1) {
        state->values[i] = (EvalValue){
            .type = TYPE_ID(Type_None),
            .format = EvalValue_Pending,
        };
    }

    state->type_pool = type_pool;
    state->intern_pool = intern_pool;
    state->decl_table = decl_table;
    state->fn_table = fn_table;

    state->ns = ns;
    state->file = file_id;
    state->scratch = sub_arena(arena, EVAL_SCRATCH_SIZE);

    state->message_count = 0;
    state->control = (EvalControl){.type = EvalControl_None};
    return state;
}

internal void
eval_add_message(EvalState *state, Message message) {
    debug_assert(state->message_count < EVAL_MAX_MESSAGE_COUNT, STR_LIT("uir message OOM"));
    state->messages[state->message_count] = message;
    state->message_count += 1;
}

internal EvalValue
eval_get_operand(EvalState *state, UirInstId inst_id) {
    debug_assert(inst_id.id != UIR_NULL.id, STR_LIT("eval get null operand"));
    if (inst_id.id == UIR_POISON.id) {
        return EVAL_VALUE_POISON;
    }
    EvalValue result = state->values[inst_id.id];
    debug_assert(result.format != EvalValue_Pending, STR_LIT("eval operand not valid"));
    return result;
}

internal b32
eval_expect_type(EvalState *state, UirInstId inst_id, TypeId *out) {
    EvalValue value = eval_get_operand(state, inst_id);
    if (value.format != EvalValue_Poison) {
        if (value.type.id == Type_TypeId && value.format == EvalValue_Type) {
            *out = value.type_val;
            return true;
        } else {
            UirInst inst = uir_get_inst(state->uir, inst_id);
            Message message = {
                .file = state->file,
                .pos = inst.pos,
                .severity = Severity_Error,
                .type = Message_ExpectedType,
            };
            eval_add_message(state, message);
        }
    }
    return false;
}

typedef enum {
    EvalMode_Exec,
    EvalMode_Emit,
} EvalMode;

internal EvalValue
eval_inst(EvalState *state, UirInstId inst_id, EvalMode mode);

internal EvalValue
eval_comp_block(EvalState *state, UirInstId inst_id) {
    UirInst inst = uir_get_inst(state->uir, inst_id);
    debug_assert(inst.type == Uir_CompBlock, STR_LIT("eval invalid comp block"));
    for (u32 i = 0; i < inst.block.count; i += 1) {
        UirInstId child_id = uir_get_block_inst_id(state->uir, inst.block, i);
        eval_inst(state, child_id, EvalMode_Exec);

        switch (state->control.type) {
            case EvalControl_None: {
            } break;

            case EvalControl_Break: {
                debug_assert(state->control.break_block.id != UIR_NULL.id,
                             STR_LIT("eval break block null"));
                if (state->control.break_block.id == inst_id.id) {
                    EvalValue result = state->control.value;
                    state->control = (EvalControl){.type = EvalControl_None};
                    return result;
                }
                return EVAL_VALUE_NORETURN;
            } break;
        }
    }
    debug_assert(false, STR_LIT("comp block end without break"));
    return (EvalValue){0};
}

internal EvalValue
eval_fn_type(EvalState *state, UirInstId inst_id, EvalMode mode) {
    Flusher flusher = flusher_init(&state->scratch);
    UirInst inst = uir_get_inst(state->uir, inst_id);
    debug_assert(inst.type == Uir_FnType, STR_LIT("eval invalid fn type"));
    EvalValue result;
    switch (mode) {
        case EvalMode_Emit: {
            debug_assert(false, STR_LIT("fn type in runtime block"));
            result = EVAL_VALUE_POISON;
        } break;
        case EvalMode_Exec: {
            b32 poisoned = false;
            TypeId *args = arena_push_array(&state->scratch, TypeId, inst.fn_type.args.count);
            for (u32 i = 0; i < inst.fn_type.args.count; i += 1) {
                UirInstId arg_inst_id = uir_get_block_inst_id(state->uir, inst.fn_type.args, i);
                TypeId arg_type;
                if (eval_expect_type(state, arg_inst_id, &arg_type)) {
                    args[i] = arg_type;
                } else {
                    poisoned = true;
                }
            }

            TypeId ret_type;
            if (!eval_expect_type(state, inst.fn_type.ret_type, &ret_type)) {
                poisoned = true;
            }

            if (poisoned) {
                result = EVAL_VALUE_POISON;
            } else {
                TypeId type_id =
                    type_pool_fn_id(state->type_pool, args, inst.fn_type.args.count, ret_type);
                result = eval_value_type(type_id);
            }
        } break;
    }
    flusher_flush(flusher);
    return result;
}

internal EvalValue
eval_break(EvalState *state, UirInstId inst_id, EvalMode mode) {
    UirInst inst = uir_get_inst(state->uir, inst_id);
    debug_assert(inst.type == Uir_Break || inst.type == Uir_BreakImplicit,
                 STR_LIT("eval invalid break"));
    EvalValue result;
    switch (mode) {
        case EvalMode_Emit: {
            // TODO: add runtime breaks
            result = EVAL_VALUE_POISON;
            UNREACHABLE;
        } break;

        case EvalMode_Exec: {
            state->control = (EvalControl){
                .type = EvalControl_Break,
                .break_block = inst.break_block.block_id,
                .value = eval_get_operand(state, inst.break_block.value),
            };
            result = EVAL_VALUE_NORETURN;
        } break;
    }
    return result;
}

internal b32
eval_resolve_intrinsic(EvalState *state, InternId intern_id, EvalValue *out) {
    Buffer name = intern_pool_get_string(state->intern_pool, intern_id);
    b32 found = true;
    if (buffer_eql(name, STR_LIT("bool"))) {
        *out = eval_value_type(TYPE_ID(Type_Bool));
    } else if (buffer_eql(name, STR_LIT("noreturn"))) {
        *out = eval_value_type(TYPE_ID(Type_NoReturn));
    } else if (buffer_eql(name, STR_LIT("void"))) {
        *out = eval_value_type(TYPE_ID(Type_Void));
    } else if (buffer_eql(name, STR_LIT("typeid"))) {
        *out = eval_value_type(TYPE_ID(Type_TypeId));
    } else if (buffer_eql(name, STR_LIT("compint"))) {
        *out = eval_value_type(TYPE_ID(Type_CompInt));
    } else if (buffer_eql(name, STR_LIT("compfloat"))) {
        *out = eval_value_type(TYPE_ID(Type_CompFloat));
    } else if (buffer_eql(name, STR_LIT("usize"))) {
        *out = eval_value_type(TYPE_ID(Type_Usize));
    } else if (buffer_eql(name, STR_LIT("isize"))) {
        *out = eval_value_type(TYPE_ID(Type_Isize));
    } else if (buffer_eql(name, STR_LIT("u8"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, false, 8));
    } else if (buffer_eql(name, STR_LIT("u16"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, false, 16));
    } else if (buffer_eql(name, STR_LIT("u32"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, false, 32));
    } else if (buffer_eql(name, STR_LIT("u64"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, false, 64));
    } else if (buffer_eql(name, STR_LIT("i8"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, true, 8));
    } else if (buffer_eql(name, STR_LIT("i16"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, true, 16));
    } else if (buffer_eql(name, STR_LIT("i32"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, true, 32));
    } else if (buffer_eql(name, STR_LIT("i64"))) {
        *out = eval_value_type(type_pool_int(state->type_pool, true, 64));
    } else {
        found = false;
    }
    return found;
}

internal EvalValue
eval_global_value(EvalState *state, UirInstId inst_id, EvalMode mode) {
    UirInst inst = uir_get_inst(state->uir, inst_id);
    debug_assert(inst.type == Uir_GlobalValue, STR_LIT("eval invalid break"));

    DeclId decl = namespace_find_name(state->ns, inst.global);
    if (decl.id == DECL_NULL.id) {
        EvalValue intrinsic;
        if (eval_resolve_intrinsic(state, inst.global, &intrinsic)) {
            return intrinsic;
        } else {
            Message message = {
                .file = state->file,
                .pos = inst.pos,
                .severity = Severity_Error,
                .type = Message_UndeclaredIdentifier,
            };
            eval_add_message(state, message);
            return EVAL_VALUE_POISON;
        }
    }

    // TODO: emit globals vals
    EvalValue result;
    switch (mode) {
        case EvalMode_Emit: {
            result = EVAL_VALUE_POISON;
        } break;
        case EvalMode_Exec: {
            result = EVAL_VALUE_POISON;
        } break;
    }
    return result;
}

internal EvalValue
eval_coerce(EvalState *state, u32 pos, EvalValue val, TypeId type_id) {
    debug_assert(val.format != EvalValue_Pending, STR_LIT("eval coerce invalid value format"));
    if (val.format == EvalValue_Poison || val.type.id == type_id.id ||
        val.type.id == Type_NoReturn) {
        return val;
    }

    if (val.type.id == Type_CompUndef) {
        val.type = type_id;
        return val;
    }

    Type t = type_pool_get_info(state->type_pool, type_id);
    if (val.type.id == Type_CompInt) {
        if (t.format == TypeFormat_FixedInt ||
            (t.format == TypeFormat_Global &&
             (t.global.id == Type_Usize || t.global.id == Type_Isize))) {
            // TODO: check if this fits
            val.type = type_id;
            return val;
        }
    }

    Message message = {
        .file = state->file,
        .pos = pos,
        .severity = Severity_Error,
        .type = Message_CoersionFailed,
    };
    eval_add_message(state, message);
    return EVAL_VALUE_POISON;
}

internal EvalValue
eval_inst(EvalState *state, UirInstId inst_id, EvalMode mode) {
    debug_assert(inst_id.id != UIR_NULL.id, STR_LIT("eval null inst"));
    if (inst_id.id == UIR_POISON.id) {
        return EVAL_VALUE_POISON;
    }

    UirInst inst = uir_get_inst(state->uir, inst_id);
    EvalValue result = EVAL_VALUE_POISON;
    switch (inst.type) {
        case Uir_CompBlock: {
            result = eval_comp_block(state, inst_id);
        } break;

        case Uir_FnArg: {
        } break;

        case Uir_FnLit: {
            switch (mode) {
                case EvalMode_Emit: {
                    Message msg = {
                        .file = state->file,
                        .pos = inst.pos,
                        .severity = Severity_Error,
                        .type = Message_TypeInRuntime,
                    };
                    eval_add_message(state, msg);
                } break;

                case EvalMode_Exec: {
                    TypeId fn_type;
                    if (!eval_expect_type(state, inst.fn_lit.fn_type, &fn_type)) {
                        debug_assert(false, STR_LIT("fn lit invalid type"));
                    }
                    debug_assert(type_pool_get_info(state->type_pool, fn_type).format ==
                                     TypeFormat_Fn,
                                 STR_LIT("fn lit invalid type"));
                    FnId fn_id = fn_table_alloc(state->fn_table, fn_type);
                    result = (EvalValue){
                        .format = EvalValue_FnLit,
                        .type = fn_type,
                        .fn_lit = fn_id,
                    };
                } break;
            }
        } break;

        case Uir_Coerce: {
            TypeId to_type;
            if (eval_expect_type(state, inst.coerce.type, &to_type)) {
                EvalValue value = eval_get_operand(state, inst.coerce.value);
                result = eval_coerce(state, inst.pos, value, to_type);
            }
        } break;

        case Uir_GlobalValue: {
            result = eval_global_value(state, inst_id, mode);
        } break;

        case Uir_NumLit: {
            debug_assert((inst.num_lit & ((u64)1 << 63)) == 0, STR_LIT("handle large ints"));
            result = (EvalValue){
                .format = EvalValue_Int,
                .type = TYPE_ID(Type_CompInt),
                .integer = (i64)inst.num_lit,
            };
        } break;

        case Uir_VoidLit: {
            result = (EvalValue){.format = EvalValue_Void, .type = TYPE_ID(Type_Void)};
        } break;

        case Uir_UndefLit: {
            result = (EvalValue){.format = EvalValue_Undef, .type = TYPE_ID(Type_CompUndef)};
        } break;

        case Uir_FnType: {
            result = eval_fn_type(state, inst_id, mode);
        } break;

        case Uir_BreakImplicit:
        case Uir_Break: {
            result = eval_break(state, inst_id, mode);
        } break;
    }

    state->values[inst_id.id] = result;
    return result;
}
