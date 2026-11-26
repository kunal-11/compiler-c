
internal Buffer uir_fmt_indent = STR_LIT("    ");

internal void
uir_print(Uir *uir, InternPool *intern_pool, UirInstId inst_id, u32 indent) {
    debug_assert(inst_id.id != UIR_NULL.id, STR_LIT("uir null inst print"));
    for (u32 i = 0; i < indent; i += 1) {
        printf("%.*s", (int)uir_fmt_indent.len, uir_fmt_indent.bytes);
    }
    if (inst_id.id == UIR_POISON.id) {
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

        case Uir_VoidLit: {
            printf("void_lit\n");
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

        case Uir_BreakImplicit:
        case Uir_Break: {
            printf("break :%%%d %%%d\n", inst.break_block.block_id.id, inst.break_block.value.id);
        } break;

        case Uir_RetImplicit:
        case Uir_Ret: {
            printf("return %%%d\n", inst.ret.id);
        } break;

        case Uir_Null:
            UNREACHABLE;
            break;
    }
}
