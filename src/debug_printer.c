internal void
print_operator(Operator op) {
    switch (op) {
        case Op_Add: {
            printf("+");
        } break;

        case Op_Negate:
        case Op_Subtract: {
            printf("-");
        } break;

        case Op_Mul: {
            printf("*");
        } break;

        case Op_Div: {
            printf("/");
        } break;

        case Op_Mod: {
            printf("%%");
        } break;

        case Op_Invalid: {
            UNREACHABLE;
        } break;
    }
}

internal Buffer tree_fmt_self_middle = STR_LIT("├── ");
internal Buffer tree_fmt_self_last = STR_LIT("└── ");

internal Buffer tree_fmt_child_middle = STR_LIT("│   ");
internal Buffer tree_fmt_child_last = STR_LIT("    ");

internal u8 ast_print_buf[2048];

internal void
ast_print_tree(Ast *ast, InternPool *intern_pool, AstNodeId node_id, u32 prefix_len, b32 is_last) {
    debug_assert(node_id.id != AST_NULL.id, STR_LIT("print null ast node"));

    Buffer child_fmt = is_last ? tree_fmt_child_last : tree_fmt_child_middle;
    for (u32 i = 0; i < child_fmt.len; i += 1) {
        debug_assert(prefix_len + i < ARRAY_COUNT(ast_print_buf), STR_LIT("ast print buf OOM"));
        ast_print_buf[prefix_len + i] = child_fmt.bytes[i];
    }
    u32 child_prefix_len = prefix_len + (u32)child_fmt.len;

    Buffer self_fmt = is_last ? tree_fmt_self_last : tree_fmt_self_middle;
    printf("%.*s%.*s", (int)prefix_len, ast_print_buf, (int)self_fmt.len, self_fmt.bytes);

    if (node_id.id == AST_POISON.id) {
        printf("POISON\n");
        return;
    }
    AstNode node = ast_get_node(ast, node_id);
    switch (node.type) {
        case Ast_ConstDecl: {
            Buffer name = intern_pool_get_string(intern_pool, node.decl.name);
            printf("const: %.*s\n", (int)name.len, name.bytes);
            if (node.decl.type.id != AST_NULL.id) {
                ast_print_tree(ast, intern_pool, node.decl.type, child_prefix_len, false);
            }
            ast_print_tree(ast, intern_pool, node.decl.value, child_prefix_len, true);
        } break;

        case Ast_VarDecl: {
            Buffer name = intern_pool_get_string(intern_pool, node.decl.name);
            printf("var: %.*s\n", (int)name.len, name.bytes);
            if (node.decl.type.id != AST_NULL.id) {
                ast_print_tree(ast, intern_pool, node.decl.type, child_prefix_len, false);
            }
            ast_print_tree(ast, intern_pool, node.decl.value, child_prefix_len, true);
        } break;

        case Ast_Identifier: {
            Buffer name = intern_pool_get_string(intern_pool, node.identifier);
            printf("identifier: %.*s\n", (int)name.len, name.bytes);
        } break;

        case Ast_FnType: {
            printf("fn_type\n");
            for (u32 i = 0; i < node.fn_type.args.count; i += 1) {
                AstNodeId arg_node = ast_get_list_node(ast, node.fn_type.args, i);
                ast_print_tree(ast, intern_pool, arg_node, child_prefix_len, false);
            }
            ast_print_tree(ast, intern_pool, node.fn_type.ret_type, child_prefix_len, true);
        } break;

        case Ast_FnArg: {
            Buffer arg_name = intern_pool_get_string(intern_pool, node.fn_arg.name);
            printf("fn_arg: %.*s\n", (int)arg_name.len, arg_name.bytes);
            ast_print_tree(ast, intern_pool, node.fn_arg.type, child_prefix_len, true);
        } break;

        case Ast_PrefixOp: {
            printf("prefix_op: ");
            print_operator(node.prefix_op.op);
            printf("\n");
            ast_print_tree(ast, intern_pool, node.prefix_op.value, child_prefix_len, true);
        } break;

        case Ast_InfixOp: {
            printf("infix_op: ");
            print_operator(node.infix_op.op);
            printf("\n");
            ast_print_tree(ast, intern_pool, node.infix_op.lhs, child_prefix_len, false);
            ast_print_tree(ast, intern_pool, node.infix_op.rhs, child_prefix_len, true);
        } break;

        case Ast_Assign: {
            printf("assignment\n");
            ast_print_tree(ast, intern_pool, node.assign.lhs, child_prefix_len, false);
            ast_print_tree(ast, intern_pool, node.assign.rhs, child_prefix_len, true);
        } break;

        case Ast_FnLit: {
            printf("fn_lit\n");
            ast_print_tree(ast, intern_pool, node.fn_lit.type, child_prefix_len,
                           node.fn_lit.body.count == 0);
            for (u32 i = 0; i < node.fn_lit.body.count; i += 1) {
                AstNodeId block_node = ast_get_list_node(ast, node.fn_lit.body, i);
                ast_print_tree(ast, intern_pool, block_node, child_prefix_len,
                               i == node.fn_lit.body.count - 1);
            }
        } break;

        case Ast_BoolLit: {
            printf("bool_lit: %d\n", node.bool_lit);
        } break;

        case Ast_NumLit: {
            printf("num_lit\n");
        } break;

        case Ast_UndefLit: {
            printf("undef_lit\n");
        } break;
    }
}
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

internal void
type_print(TypePool *pool, TypeId id) {
    Type type = type_pool_get_info(pool, id);
    switch (type.format) {
        case TypeFormat_Global: {
            switch (type.global.id) {
                case Type_None: {
                    printf("none");
                } break;
                case Type_Void: {
                    printf("void");
                } break;
                case Type_NoReturn: {
                    printf("noreturn");
                } break;
                case Type_TypeId: {
                    printf("typeid");
                } break;
                case Type_Bool: {
                    printf("bool");
                } break;
                case Type_CompUndef: {
                    printf("comp_undef");
                } break;
                case Type_CompInt: {
                    printf("comp_int");
                } break;
                case Type_CompFloat: {
                    printf("comp_float");
                } break;
                case Type_Isize: {
                    printf("isize");
                } break;
                case Type_Usize: {
                    printf("usize");
                } break;
                default:
                    UNREACHABLE;
            }
        } break;

        case TypeFormat_FixedInt: {
            if (type.fixed_int.is_signed) {
                printf("i");
            } else {
                printf("u");
            }
            printf("%d", type.fixed_int.width);
        } break;

        case TypeFormat_Fn: {
            printf("fn ( ");
            for (u32 i = 0; i < type.fn.args.count; i += 1) {
                TypeId arg = type_pool_get_block_type(pool, type.fn.args, i);
                type_print(pool, arg);
                printf(" ");
            }
            printf(") ");
            type_print(pool, type.fn.ret);
        } break;

        case TypeFormat_Ptr: {
            printf("*");
            type_print(pool, type.ptr_info.to);
        } break;

        case TypeFormat_Invalid:
        case TypeFormat_Float: {
            UNREACHABLE;
        } break;
    }
}

internal void
eval_value_print(TypePool *type_pool, EvalValue val) {
    switch (val.format) {
        case EvalValue_Pending: {
            printf("Pending");
        } break;

        case EvalValue_Poison: {
            printf("Poison");
        } break;

        case EvalValue_NoReturn: {
            printf("NoReturn");
        } break;

        case EvalValue_Void: {
            printf("Void");
        } break;

        case EvalValue_Undef: {
            printf("Undefined");
        } break;

        case EvalValue_Bool: {
            printf("Bool: ");
            if (val.boolean) {
                printf("true");
            } else {
                printf("false");
            }
        } break;

        case EvalValue_Int: {
            printf("Int: %li", val.integer);
        } break;

        case EvalValue_Type: {
            printf("Type: ");
            type_print(type_pool, val.type_val);
        } break;

        case EvalValue_FnLit: {
            printf("Fn id: %d", val.fn_lit.id);
        } break;

        case EvalValue_Runtime: {
            UNREACHABLE;
        } break;
    }
}

internal void
eval_frame_print(TypePool *type_pool, EvalFrame *frame) {
    for (u32 i = 2; i < frame->uir.inst_count; i += 1) {
        printf("%%%d: ", i);
        eval_value_print(type_pool, frame->values[i]);
        printf("\n");
    }
}
