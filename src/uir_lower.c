#include "uir.c"

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
            .intern_id = INTERN_NULL,
            .inst_id = UIR_NULL,
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
        if (name.intern_id.id == INTERN_NULL.id || name.intern_id.id == intern_id.id) {
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
    if (slot->intern_id.id != INTERN_NULL.id && slot->lexical_depth == name.lexical_depth) {
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
    if (slot->intern_id.id == INTERN_NULL.id) {
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

typedef enum {
    Block_Comptime,
    Block_FnLit,
} BlockType;

typedef struct {
    BlockType type;

    UirInstId *insts;
    Flusher flusher;

    u32 name_undo_marker;

    // NULL incase of nested Block_Comptime
    UirInstId inst_id;

    // below fields are only valid on a Block_FnLit
    u32 prev_comp_nests;
    UirInstId fn_lit_type;
} Block;

#define UIR_MAX_BLOCK_DEPTH 2048
#define UIR_SCRATCH_SIZE (1 * MB)

#define UIR_MAX_MESSAGE_COUNT 1024

typedef struct {
    Uir *uir;
    Ast *ast;

    FileId file_id;
    Buffer file_contents;

    Arena scratch;
    u32 comp_nests;

    Block blocks[UIR_MAX_BLOCK_DEPTH];
    u32 block_count;

    UirNameTable name_table;
    u32 fn_depth;

    Message messages[UIR_MAX_MESSAGE_COUNT];
    u32 message_count;
} UirLowerState;

internal UirLowerState *
uir_lower_init(Arena *arena, Ast *ast, FileId file_id, Buffer file_contents) {
    UirLowerState *state = arena_push_struct(arena, UirLowerState);
    state->uir = uir_init(arena);
    state->ast = ast;
    state->file_id = file_id;
    state->file_contents = file_contents;
    state->scratch = sub_arena(arena, UIR_SCRATCH_SIZE);
    state->comp_nests = 0;
    state->block_count = 0;
    state->message_count = 0;
    uir_name_table_init(&state->name_table, arena);
    state->fn_depth = 0;
    return state;
}

internal void
uir_lower_add_message(UirLowerState *state, Message message) {
    debug_assert(state->message_count < UIR_MAX_MESSAGE_COUNT, STR_LIT("uir message OOM"));
    state->messages[state->message_count] = message;
    state->message_count += 1;
}

internal UirInstId
uir_lower_emit(UirLowerState *state, UirInst inst) {
    UirInstId inst_id = uir_alloc_inst(state->uir, inst);
    *arena_push_struct(&state->scratch, UirInstId) = inst_id;
    return inst_id;
}

internal void
uir_lower_begin_comp_block(UirLowerState *state) {
    debug_assert(state->block_count < UIR_MAX_BLOCK_DEPTH, STR_LIT("uir block stack OOM"));
    UirInstId block_inst = UIR_NULL;
    if (state->comp_nests == 0) {
        block_inst = uir_lower_emit(state, (UirInst){.type = Uir_Null});
    }
    state->comp_nests += 1;
    state->blocks[state->block_count] = (Block){
        .type = Block_Comptime,
        .flusher = flusher_init(&state->scratch),
        .insts = arena_peek_struct(&state->scratch, UirInstId),
        .inst_id = block_inst,
        .name_undo_marker = state->name_table.undo_count,
    };
    state->block_count += 1;
}

internal void
uir_lower_begin_fn_lit(UirLowerState *state, UirInstId type) {
    debug_assert(state->block_count < UIR_MAX_BLOCK_DEPTH, STR_LIT("uir block stack OOM"));
    UirInstId block_inst = uir_lower_emit(state, (UirInst){.type = Uir_Null});
    state->blocks[state->block_count] = (Block){
        .type = Block_FnLit,
        .flusher = flusher_init(&state->scratch),
        .insts = arena_peek_struct(&state->scratch, UirInstId),
        .inst_id = block_inst,
        .prev_comp_nests = state->comp_nests,
        .fn_lit_type = type,
        .name_undo_marker = state->name_table.undo_count,
    };
    state->block_count += 1;
    state->comp_nests = 0;
    state->fn_depth += 1;
}

internal UirInstId
uir_lower_end_block(UirLowerState *state, u32 pos, UirInstId res_inst) {
    debug_assert(state->block_count > 0, STR_LIT("no block to emit"));
    Block block = state->blocks[state->block_count - 1];
    state->block_count -= 1;
    uir_name_table_undo(&state->name_table, block.name_undo_marker);

    if (block.type == Block_Comptime) {
        state->comp_nests -= 1;
        if (state->comp_nests > 0) {
            debug_assert(block.inst_id.id == UIR_NULL.id, STR_LIT("nested comp block null"));
            return res_inst;
        }
    }

    switch (block.type) {
        case Block_Comptime: {
            if (res_inst.id == UIR_NULL.id) {
                UirInst void_inst = {.pos = pos, .type = Uir_VoidLit};
                res_inst = uir_lower_emit(state, void_inst);
            }
            UirInst break_inst = {
                .pos = pos,
                .type = Uir_BreakImplicit,
                .break_block = {.block_id = block.inst_id, .value = res_inst},
            };
            uir_lower_emit(state, break_inst);

            u32 inst_count = (u32)arena_list_count(&state->scratch, block.insts);
            UirBlock extra_block = uir_alloc_block(state->uir, inst_count);
            for (u32 i = 0; i < inst_count; i += 1) {
                uir_write_block_inst(state->uir, extra_block, i, block.insts[i]);
            }
            UirInst block_inst = {
                .pos = pos,
                .type = Uir_CompBlock,
                .block = extra_block,
            };
            uir_overwrite_inst(state->uir, block.inst_id, block_inst);
        } break;

        case Block_FnLit: {
            debug_assert(res_inst.id == UIR_NULL.id, STR_LIT("invalid res type on fn_lit"));
            UirInst void_inst = {.pos = pos, .type = Uir_VoidLit};
            res_inst = uir_lower_emit(state, void_inst);
            UirInst ret_inst = {
                .pos = pos,
                .type = Uir_RetImplicit,
                .ret = res_inst,
            };
            uir_lower_emit(state, ret_inst);
            u32 inst_count = (u32)arena_list_count(&state->scratch, block.insts);
            UirBlock extra_block = uir_alloc_block(state->uir, inst_count);
            for (u32 i = 0; i < inst_count; i += 1) {
                uir_write_block_inst(state->uir, extra_block, i, block.insts[i]);
            }
            UirInst fn_lit_inst = {
                .pos = pos,
                .type = Uir_FnLit,
                .fn_lit = {.fn_type = block.fn_lit_type, .body = extra_block},
            };
            uir_overwrite_inst(state->uir, block.inst_id, fn_lit_inst);
            state->comp_nests = block.prev_comp_nests;
            state->fn_depth -= 1;
        } break;
    }

    flusher_flush(block.flusher);
    debug_assert(block.inst_id.id != UIR_NULL.id, STR_LIT("uir block id null"));
    return block.inst_id;
}

internal UirInstId
uir_lower_node_pointer(UirLowerState *state, AstNodeId node_id) {
    debug_assert(node_id.id != AST_NULL.id, STR_LIT("uir lower null node"));
    if (node_id.id == AST_POISON.id) {
        return UIR_POISON;
    }
    AstNode node = ast_get_node(state->ast, node_id);
    UirInstId result = UIR_POISON;
    switch (node.type) {
        case Ast_Identifier: {
            UirNameFindResult name =
                uir_name_table_find_name(&state->name_table, node.identifier, state->fn_depth);
            switch (name.tag) {
                case UirFindName_Closure: {
                    Message msg = {
                        .file = state->file_id,
                        .pos = node.pos,
                        .severity = Severity_Error,
                        .type = Message_RuntimeClosure,
                        .original_declaration = name.name.pos,
                    };
                    uir_lower_add_message(state, msg);
                } break;

                case UirFindName_Success: {
                    result = name.name.inst_id;
                    if (name.name.type != UirNameType_Var) {
                        Message msg = {
                            .file = state->file_id,
                            .pos = node.pos,
                            .severity = Severity_Error,
                            .type = Message_LvalueConstant,
                        };
                        uir_lower_add_message(state, msg);
                        result = UIR_POISON;
                    }
                } break;

                case UirFindName_Empty: {
                    UirInst inst = {
                        .pos = node.pos,
                        .type = Uir_GlobalPointer,
                        .global = node.identifier,
                    };
                    result = uir_lower_emit(state, inst);
                } break;
            }
        } break;

        default: {
            Message msg = {
                .file = state->file_id,
                .pos = node.pos,
                .severity = Severity_Error,
                .type = Message_LvalueInvalid,
            };
            uir_lower_add_message(state, msg);
        } break;
    }
    return result;
}

internal UirInstId
uir_lower_node(UirLowerState *state, AstNodeId node_id, UirInstId result_type) {
    debug_assert(node_id.id != AST_NULL.id, STR_LIT("uir lower null node"));
    if (node_id.id == AST_POISON.id) {
        return UIR_POISON;
    }

    AstNode node = ast_get_node(state->ast, node_id);
    UirInstId result = UIR_POISON;
    switch (node.type) {
        case Ast_UndefLit: {
            UirInst inst = {
                .pos = node.pos,
                .type = Uir_UndefLit,
            };
            result = uir_lower_emit(state, inst);
        } break;

        case Ast_NumLit: {
            Token token = lexer_lex_token(state->file_contents, node.pos);
            Buffer lit_str = buffer_slice(state->file_contents, token.begin, token.end);
            u64 num;
            if (!parse_u64(lit_str, &num)) {
                Message message = {
                    .file = state->file_id,
                    .pos = node.pos,
                    .severity = Severity_Error,
                    .type = Message_MalformedNumLiteral,
                };
                uir_lower_add_message(state, message);
            } else {
                UirInst inst = {
                    .pos = node.pos,
                    .type = Uir_NumLit,
                    .num_lit = num,
                };
                result = uir_lower_emit(state, inst);
            }
        } break;

        case Ast_BoolLit: {
            UirInst inst = {
                .pos = node.pos,
                .type = Uir_BoolLit,
                .bool_lit = node.bool_lit,
            };
            result = uir_lower_emit(state, inst);
        } break;

        case Ast_Identifier: {
            UirNameFindResult name =
                uir_name_table_find_name(&state->name_table, node.identifier, state->fn_depth);
            switch (name.tag) {
                case UirFindName_Closure: {
                    Message msg = {
                        .file = state->file_id,
                        .pos = node.pos,
                        .severity = Severity_Error,
                        .type = Message_RuntimeClosure,
                        .original_declaration = name.name.pos,
                    };
                    uir_lower_add_message(state, msg);
                } break;

                case UirFindName_Success: {
                    result = name.name.inst_id;
                    if (name.name.type == UirNameType_Var) {
                        UirInst load_inst = {
                            .pos = node.pos,
                            .type = Uir_Load,
                            .load = result,
                        };
                        result = uir_lower_emit(state, load_inst);
                    }
                } break;

                case UirFindName_Empty: {
                    UirInst inst = {
                        .pos = node.pos,
                        .type = Uir_GlobalValue,
                        .global = node.identifier,
                    };
                    result = uir_lower_emit(state, inst);
                } break;
            }
        } break;

        case Ast_ConstDecl: {
            uir_lower_begin_comp_block(state);
            UirInstId type = UIR_NULL;
            if (node.decl.type.id != AST_NULL.id) {
                type = uir_lower_node(state, node.decl.type, UIR_NULL);
            }
            UirInstId value = uir_lower_node(state, node.decl.value, type);
            result = uir_lower_end_block(state, node.pos, value);

            UirName name = {
                .pos = node.pos,
                .intern_id = node.decl.name,
                .inst_id = result,
                .type = UirNameType_Const,
                .lexical_depth = state->block_count,
                .fn_depth = state->fn_depth,
            };
            UirNameAddResult name_res = uir_name_table_add_name(&state->name_table, name);
            if (name_res.redeclared) {
                Message msg = {
                    .file = state->file_id,
                    .pos = node.pos,
                    .severity = Severity_Error,
                    .type = Message_RedeclaredName,
                    .original_declaration = name_res.old_name.pos,
                };
                uir_lower_add_message(state, msg);
            }
        } break;

        case Ast_VarDecl: {
            UirInstId type = UIR_NULL;
            if (node.decl.type.id != AST_NULL.id) {
                uir_lower_begin_comp_block(state);
                type = uir_lower_node(state, node.decl.type, UIR_NULL);
                type = uir_lower_end_block(state, node.pos, type);
            }
            UirInstId value = uir_lower_node(state, node.decl.value, type);
            if (type.id == UIR_NULL.id) {
                UirInst type_of = {
                    .pos = node.pos,
                    .type = Uir_TypeOf,
                    .type_of = value,
                };
                type = uir_lower_emit(state, type_of);
            }
            UirInst alloc_inst = {
                .pos = node.pos,
                .type = Uir_Alloc,
                .alloc_type = type,
            };
            result = uir_lower_emit(state, alloc_inst);

            UirInst store_inst = {
                .pos = node.pos,
                .type = Uir_Store,
                .store = {.lhs = result, .rhs = value},
            };
            uir_lower_emit(state, store_inst);

            UirName name = {
                .pos = node.pos,
                .intern_id = node.decl.name,
                .inst_id = result,
                .type = UirNameType_Var,
                .lexical_depth = state->block_count,
                .fn_depth = state->fn_depth,
            };
            UirNameAddResult name_res = uir_name_table_add_name(&state->name_table, name);
            if (name_res.redeclared) {
                Message msg = {
                    .file = state->file_id,
                    .pos = node.pos,
                    .severity = Severity_Error,
                    .type = Message_RedeclaredName,
                    .original_declaration = name_res.old_name.pos,
                };
                uir_lower_add_message(state, msg);
            }
        } break;

        case Ast_Assign: {
            UirInstId lhs = uir_lower_node_pointer(state, node.assign.lhs);
            UirInstId rhs = uir_lower_node(state, node.assign.rhs, UIR_NULL);
            UirInst inst = {
                .pos = node.pos,
                .type = Uir_Store,
                .store = {.lhs = lhs, .rhs = rhs},
            };
            result = uir_lower_emit(state, inst);
        } break;

        case Ast_InfixOp: {
            UirInstId lhs = uir_lower_node(state, node.infix_op.lhs, UIR_NULL);
            UirInstId rhs = uir_lower_node(state, node.infix_op.rhs, UIR_NULL);
            UirInst inst = {
                .pos = node.pos,
                .type = Uir_InfixOp,
                .infix_op = {.op = node.infix_op.op, .lhs = lhs, .rhs = rhs},
            };
            result = uir_lower_emit(state, inst);
        } break;

        case Ast_PrefixOp: {
            UirInstId value = uir_lower_node(state, node.prefix_op.value, UIR_NULL);
            UirInst inst = {
                .pos = node.pos,
                .type = Uir_PrefixOp,
                .prefix_op = {.op = node.prefix_op.op, .value = value},
            };
            result = uir_lower_emit(state, inst);
        } break;

        case Ast_FnType: {
            uir_lower_begin_comp_block(state);
            UirBlock args_block = uir_alloc_block(state->uir, node.fn_type.arg_count);
            for (u32 i = 0; i < args_block.count; i += 1) {
                AstNodeId arg_node_id = ast_get_fn_arg_id(state->ast, node.fn_type, i);
                UirInstId arg_type = UIR_POISON;
                if (arg_node_id.id != AST_POISON.id) {
                    AstNode arg_node = ast_get_node(state->ast, arg_node_id);
                    debug_assert(arg_node.type == Ast_FnArg, STR_LIT("ast invalid fn arg node"));
                    arg_type = uir_lower_node(state, arg_node.fn_arg.type, UIR_NULL);
                }
                uir_write_block_inst(state->uir, args_block, i, arg_type);
            }
            UirInstId ret_type = uir_lower_node(state, node.fn_type.ret_type, UIR_NULL);
            UirInst fn_type_inst = {
                .pos = node.pos,
                .type = Uir_FnType,
                .fn_type = {.args = args_block, .ret_type = ret_type},
            };
            UirInstId res_inst_id = uir_lower_emit(state, fn_type_inst);
            result = uir_lower_end_block(state, node.pos, res_inst_id);
        } break;

        case Ast_FnLit: {
            UirInstId type = uir_lower_node(state, node.fn_lit.type, UIR_NULL);
            uir_lower_begin_fn_lit(state, type);

            if (node.fn_lit.type.id != AST_POISON.id) {
                AstNode fn_type_node = ast_get_node(state->ast, node.fn_lit.type);
                debug_assert(fn_type_node.type == Ast_FnType, STR_LIT("ast fn type invalid"));
                for (u32 i = 0; i < fn_type_node.fn_type.arg_count; i += 1) {
                    AstNodeId arg_node_id = ast_get_fn_arg_id(state->ast, fn_type_node.fn_type, i);
                    if (arg_node_id.id != AST_POISON.id) {
                        AstNode arg_node = ast_get_node(state->ast, arg_node_id);
                        debug_assert(arg_node.type == Ast_FnArg, STR_LIT("ast fn arg invalid"));
                        UirInst arg_inst = {
                            .pos = arg_node.pos,
                            .type = Uir_FnArg,
                            .fn_arg_idx = i,
                        };
                        UirInstId arg_inst_id = uir_lower_emit(state, arg_inst);
                        UirName arg_name = {
                            .pos = arg_node.pos,
                            .intern_id = arg_node.fn_arg.name,
                            .inst_id = arg_inst_id,
                            .type = UirNameType_Arg,
                            .lexical_depth = state->block_count,
                            .fn_depth = state->fn_depth,
                        };
                        UirNameAddResult name_res =
                            uir_name_table_add_name(&state->name_table, arg_name);
                        if (name_res.redeclared) {
                            Message msg = {
                                .file = state->file_id,
                                .pos = arg_node.pos,
                                .severity = Severity_Error,
                                .type = Message_RedeclaredName,
                                .original_declaration = name_res.old_name.pos,
                            };
                            uir_lower_add_message(state, msg);
                        }
                    }
                }
            }

            for (u32 i = 0; i < node.fn_lit.body.count; i += 1) {
                AstNodeId body_node = ast_get_block_node(state->ast, node.fn_lit.body, i);
                uir_lower_node(state, body_node, UIR_NULL);
            }
            result = uir_lower_end_block(state, node.pos, UIR_NULL);
        } break;

        case Ast_FnArg: {
            UNREACHABLE;
        } break;
    }

    if (result_type.id != UIR_NULL.id) {
        UirInst inst = {
            .pos = node.pos,
            .type = Uir_Coerce,
            .coerce = {.type = result_type, .value = result},
        };
        result = uir_lower_emit(state, inst);
    }

    return result;
}

internal UirInstId
uir_lower_global(UirLowerState *state, AstNodeId node_id) {
    debug_assert(node_id.id != AST_NULL.id, STR_LIT("uir lower global null node"));
    if (node_id.id == AST_POISON.id) {
        return UIR_POISON;
    }
    AstNode node = ast_get_node(state->ast, node_id);
    Flusher flusher = flusher_init(&state->scratch);

    UirInstId result = UIR_POISON;
    switch (node.type) {
        case Ast_ConstDecl:
        case Ast_VarDecl: {
            uir_lower_begin_comp_block(state);
            AstDecl decl = node.decl;
            UirInstId type_inst = UIR_NULL;
            if (decl.type.id != AST_NULL.id) {
                type_inst = uir_lower_node(state, decl.type, UIR_NULL);
            }
            UirInstId value_inst = uir_lower_node(state, decl.value, type_inst);
            result = uir_lower_end_block(state, node.pos, value_inst);
        } break;

        default: {
            debug_assert(false, STR_LIT("only decls are supported on global for now"));
        } break;
    }

    flusher_flush(flusher);
    return result;
}
