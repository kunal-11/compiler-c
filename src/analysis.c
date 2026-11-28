// namespace
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

// fn table
internal FnTable *
fn_table_init(Arena *arena) {
    FnTable *table = arena_push_struct(arena, FnTable);
    table->fns = arena_push_array(arena, Fn, FN_TABLE_CAPACITY);
    table->capacity = FN_TABLE_CAPACITY;
    table->count = 1;
    return table;
}

internal FnId
fn_table_alloc(FnTable *table, TypeId type) {
    debug_assert(table->count < table->capacity, STR_LIT("fn table OOM"));
    FnId result = {.id = table->count};
    table->count += 1;
    table->fns[result.id] = (Fn){
        .type = type,
        .status = FnStatus_Ready,
    };
    return result;
}

internal Fn *
fn_table_get_fn(FnTable *table, FnId id) {
    debug_assert(id.id != FN_NULL.id, STR_LIT("fn table get null id"));
    debug_assert(id.id < table->count, STR_LIT("fn table id OOB"));
    return table->fns + id.id;
}

// decl table
internal DeclTable *
decl_table_init(Arena *arena) {
    DeclTable *table = arena_push_struct(arena, DeclTable);
    table->decls = arena_push_array(arena, Decl, DECL_TABLE_CAPACITY);
    table->capacity = DECL_TABLE_CAPACITY;
    table->count = 1;
    return table;
}

internal DeclId
decl_table_alloc(DeclTable *table, FileId file, Uir uir, Namespace *ns, UirInstId uir_inst) {
    debug_assert(table->count < table->capacity, STR_LIT("decl table OOM"));
    DeclId result = {.id = table->count};
    table->count += 1;
    table->decls[result.id] = (Decl){
        .file = file,
        .uir = uir,
        .ns = ns,
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

// eval
internal void
analysis_add_message(AnalysisState *state, Message message);

internal EvalFrame *
eval_frame_init(Arena *arena, Namespace *ns, FileId file, Uir uir) {
    EvalFrame *frame = arena_push_struct(arena, EvalFrame);
    frame->file = file;
    frame->ns = ns;
    frame->uir = uir;
    frame->values = arena_push_array(arena, EvalValue, uir.inst_count);
    for (u32 i = 0; i < uir.inst_count; i += 1) {
        frame->values[i] = (EvalValue){
            .type = TYPE_ID(Type_None),
            .format = EvalValue_Pending,
        };
    }
    frame->control = (EvalControl){.type = EvalControl_None};
    return frame;
}

internal EvalValue
eval_value_type(TypeId type) {
    debug_assert(type.id != Type_None, STR_LIT("eval invalid type value"));
    return (EvalValue){
        .format = EvalValue_Type,
        .type = TYPE_ID(Type_TypeId),
        .type_val = type,
    };
}

internal EvalValue
eval_get_operand(EvalFrame *frame, UirInstId inst_id) {
    debug_assert(inst_id.id != UIR_NULL.id, STR_LIT("eval get null operand"));
    if (inst_id.id == UIR_POISON.id) {
        return EVAL_VALUE_POISON;
    }
    debug_assert(inst_id.id < frame->uir.inst_count, STR_LIT("eval get inst id OOB"));

    EvalValue result = frame->values[inst_id.id];
    debug_assert(result.format != EvalValue_Pending, STR_LIT("eval operand not valid"));
    return result;
}

internal b32
eval_expect_type(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, TypeId *out) {
    EvalValue value = eval_get_operand(frame, inst_id);
    if (value.format != EvalValue_Poison) {
        if (value.type.id == Type_TypeId && value.format == EvalValue_Type) {
            *out = value.type_val;
            return true;
        } else {
            UirInst inst = uir_get_inst(&frame->uir, inst_id);
            Message message = {
                .file = frame->file,
                .pos = inst.pos,
                .severity = Severity_Error,
                .type = Message_ExpectedType,
            };
            analysis_add_message(state, message);
        }
    }
    return false;
}

typedef enum {
    EvalMode_Exec,
    EvalMode_Emit,
} EvalMode;

internal EvalValue
eval_inst(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, EvalMode mode);

internal EvalValue
eval_comp_block(AnalysisState *state, EvalFrame *frame, UirInstId inst_id) {
    UirInst inst = uir_get_inst(&frame->uir, inst_id);
    debug_assert(inst.type == Uir_CompBlock, STR_LIT("eval invalid comp block"));
    for (u32 i = 0; i < inst.block.count; i += 1) {
        UirInstId child_id = uir_get_block_inst_id(&frame->uir, inst.block, i);
        eval_inst(state, frame, child_id, EvalMode_Exec);

        switch (frame->control.type) {
            case EvalControl_None: {
            } break;

            case EvalControl_Break: {
                debug_assert(frame->control.break_block.id != UIR_NULL.id,
                             STR_LIT("eval break block null"));
                if (frame->control.break_block.id == inst_id.id) {
                    EvalValue result = frame->control.value;
                    frame->control = (EvalControl){.type = EvalControl_None};
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
eval_fn_type(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, EvalMode mode) {
    Flusher flusher = flusher_init(&state->scratch);
    UirInst inst = uir_get_inst(&frame->uir, inst_id);
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
                UirInstId arg_inst_id = uir_get_block_inst_id(&frame->uir, inst.fn_type.args, i);
                TypeId arg_type;
                if (eval_expect_type(state, frame, arg_inst_id, &arg_type)) {
                    args[i] = arg_type;
                } else {
                    poisoned = true;
                }
            }

            TypeId ret_type;
            if (!eval_expect_type(state, frame, inst.fn_type.ret_type, &ret_type)) {
                poisoned = true;
            }

            if (poisoned) {
                result = EVAL_VALUE_POISON;
            } else {
                TypeId type_id =
                    type_pool_fn(state->type_pool, args, inst.fn_type.args.count, ret_type);
                result = eval_value_type(type_id);
            }
        } break;
    }
    flusher_flush(flusher);
    return result;
}

internal EvalValue
eval_break(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, EvalMode mode) {
    UirInst inst = uir_get_inst(&frame->uir, inst_id);
    debug_assert(inst.type == Uir_Break || inst.type == Uir_BreakImplicit,
                 STR_LIT("eval invalid break"));
    EvalValue result;
    switch (mode) {
        case EvalMode_Emit: {
            // TODO: add runtime breaks
            (void)state;
            result = EVAL_VALUE_POISON;
            UNREACHABLE;
        } break;

        case EvalMode_Exec: {
            frame->control = (EvalControl){
                .type = EvalControl_Break,
                .break_block = inst.break_block.block_id,
                .value = eval_get_operand(frame, inst.break_block.value),
            };
            result = EVAL_VALUE_NORETURN;
        } break;
    }
    return result;
}

internal b32
eval_resolve_intrinsic(AnalysisState *state, InternId intern_id, EvalValue *out) {
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
eval_global_value(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, EvalMode mode) {
    UirInst inst = uir_get_inst(&frame->uir, inst_id);
    debug_assert(inst.type == Uir_GlobalValue, STR_LIT("eval invalid break"));

    DeclId decl = namespace_find_name(frame->ns, inst.global);
    if (decl.id == DECL_NULL.id) {
        EvalValue intrinsic;
        if (eval_resolve_intrinsic(state, inst.global, &intrinsic)) {
            return intrinsic;
        } else {
            Message message = {
                .file = frame->file,
                .pos = inst.pos,
                .severity = Severity_Error,
                .type = Message_UndeclaredIdentifier,
            };
            analysis_add_message(state, message);
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
eval_coerce(AnalysisState *state, EvalFrame *frame, u32 pos, EvalValue val, TypeId type_id) {
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
        .file = frame->file,
        .pos = pos,
        .severity = Severity_Error,
        .type = Message_CoersionFailed,
    };
    analysis_add_message(state, message);
    return EVAL_VALUE_POISON;
}

internal EvalValue
eval_inst(AnalysisState *state, EvalFrame *frame, UirInstId inst_id, EvalMode mode) {
    debug_assert(inst_id.id != UIR_NULL.id, STR_LIT("eval null inst"));
    if (inst_id.id == UIR_POISON.id) {
        return EVAL_VALUE_POISON;
    }

    UirInst inst = uir_get_inst(&frame->uir, inst_id);
    EvalValue result = EVAL_VALUE_POISON;
    switch (inst.type) {
        case Uir_CompBlock: {
            result = eval_comp_block(state, frame, inst_id);
        } break;

        case Uir_FnArg: {
        } break;

        case Uir_FnLit: {
            switch (mode) {
                case EvalMode_Emit: {
                    Message msg = {
                        .file = frame->file,
                        .pos = inst.pos,
                        .severity = Severity_Error,
                        .type = Message_TypeInRuntime,
                    };
                    analysis_add_message(state, msg);
                } break;

                case EvalMode_Exec: {
                    TypeId fn_type;
                    if (!eval_expect_type(state, frame, inst.fn_lit.fn_type, &fn_type)) {
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
            if (eval_expect_type(state, frame, inst.coerce.type, &to_type)) {
                EvalValue value = eval_get_operand(frame, inst.coerce.value);
                result = eval_coerce(state, frame, inst.pos, value, to_type);
            }
        } break;

        case Uir_GlobalValue: {
            result = eval_global_value(state, frame, inst_id, mode);
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
            result = eval_fn_type(state, frame, inst_id, mode);
        } break;

        case Uir_BreakImplicit:
        case Uir_Break: {
            result = eval_break(state, frame, inst_id, mode);
        } break;
    }

    frame->values[inst_id.id] = result;
    return result;
}

// analysis
internal void
analysis_queue_push_entry(AnalysisQueue *queue, AnalysisEntry entry) {
    debug_assert(queue->queued_count < queue->capacity, STR_LIT("analysis queue OOM"));
    queue->entries[(queue->read_idx + queue->queued_count) % queue->capacity] = entry;
    queue->queued_count += 1;
}

internal AnalysisEntry
analysis_queue_pop_entry(AnalysisQueue *queue) {
    debug_assert(queue->queued_count > 0, STR_LIT("analysis pop on empty queue"));
    AnalysisEntry entry = queue->entries[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % queue->capacity;
    queue->queued_count -= 1;
    return entry;
}

internal void
analysis_add_message(AnalysisState *state, Message message) {
    debug_assert(state->message_count < EVAL_MAX_MESSAGE_COUNT, STR_LIT("uir message OOM"));
    state->messages[state->message_count] = message;
    state->message_count += 1;
}

internal AnalysisState *
analysis_state_init(Arena *arena, InternPool *intern_pool) {
    AnalysisState *state = arena_push_struct(arena, AnalysisState);
    state->scratch = sub_arena(arena, ANALYSIS_SCRATCH_SIZE);

    state->intern_pool = intern_pool;
    state->decl_table = decl_table_init(arena);
    state->type_pool = type_pool_init(arena);
    state->fn_table = fn_table_init(arena);

    state->tir_insts = arena_push_array(arena, TirInst, ANALYSIS_TIR_INST_CAPACITY);
    state->inst_count = 1;
    state->inst_capacity = ANALYSIS_TIR_INST_CAPACITY;

    state->tir_extras = arena_push_array(arena, u32, ANALYSIS_TIR_EXTRA_CAPACITY);
    state->extra_count = 0;
    state->extra_capacity = ANALYSIS_TIR_EXTRA_CAPACITY;

    state->queue = (AnalysisQueue){
        .entries = arena_push_array(arena, AnalysisEntry, ANALYSIS_QUEUE_CAPACITY),
        .capacity = ANALYSIS_QUEUE_CAPACITY,
        .read_idx = 0,
        .queued_count = 0,
    };

    state->message_count = 0;
    return state;
}

internal void
analysis_queue_decl(AnalysisState *state, DeclId decl_id) {
    AnalysisEntry entry = {
        .type = AnalysisType_Decl,
        .decl = decl_id,
    };
    analysis_queue_push_entry(&state->queue, entry);

    Decl *decl = decl_table_get_decl(state->decl_table, decl_id);
    decl->status = DeclStatus_Queued;
}

internal void
analysis_queue_fn(AnalysisState *state, FnId fn_id) {
    AnalysisEntry entry = {
        .type = AnalysisType_Fn,
        .fn = fn_id,
    };
    analysis_queue_push_entry(&state->queue, entry);

    Fn *fn = fn_table_get_fn(state->fn_table, fn_id);
    fn->status = FnStatus_Queued;
}

#include "debug_printer.c"

internal void
analysis_analyze(AnalysisState *state) {
    while (state->queue.queued_count > 0) {
        AnalysisEntry entry = analysis_queue_pop_entry(&state->queue);
        switch (entry.type) {
            case AnalysisType_Decl: {
                Decl *decl = decl_table_get_decl(state->decl_table, entry.decl);

                EvalFrame *frame =
                    eval_frame_init(&state->scratch, decl->ns, decl->file, decl->uir);
                EvalValue value = eval_inst(state, frame, decl->uir_inst, EvalMode_Exec);
                printf("\nEval:\n");
                for (u32 i = 2; i < frame->uir.inst_count; i += 1) {
                    printf("%%%d: ", i);
                    eval_print_value(state->type_pool, frame->values[i]);
                    printf("\n");
                }

                printf("\nResult: ");
                eval_print_value(state->type_pool, value);
                printf("\n\n");

                if (value.format == EvalValue_FnLit) {
                    analysis_queue_fn(state, value.fn_lit);
                }
            } break;

            case AnalysisType_Fn: {
                Fn *fn = fn_table_get_fn(state->fn_table, entry.fn);
            } break;
        }
    }
}
