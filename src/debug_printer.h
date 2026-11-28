
internal void
ast_print_tree(Ast *ast, InternPool *intern_pool, AstNodeId node_id, u32 prefix_len, b32 is_last);

internal void
uir_print(Uir *uir, InternPool *intern_pool, UirInstId inst_id, u32 indent);
