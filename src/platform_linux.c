#include "compiler.c"

// #include <stdio.h>
#include <stdlib.h>

char *test_path = "testdata/mod1/test.pnx";

internal Buffer
read_full_file(Arena *arena, char *path) {
    Buffer result = {0};

    FILE *f = fopen(path, "rb");
    if (f == 0) {
        debug_log_error(STR_LIT("error opening file"));
        goto cleanup;
    }

    usize len = 0;
    usize bytes_read = 0;
    while ((bytes_read = fread(arena->bytes + arena->used + len, 1, 1024, f)) > 0) {
        len += bytes_read;
    }

    if (ferror(f)) {
        debug_log_error(STR_LIT("error reading file"));
        goto cleanup;
    }
    result = (Buffer){.bytes = arena_push_array(arena, u8, len), .len = len};
cleanup:
    if (f != 0)
        fclose(f);
    return result;
}

#define MEM_SIZE (32 * MB)

int
main(void) {
    u8 *memory = (u8 *)malloc(MEM_SIZE);
    if (memory == 0) {
        debug_log_error(STR_LIT("memory alloc failed"));
        goto cleanup;
    }
    Arena arena = {.bytes = memory, .max_len = MEM_SIZE};

    InternPool *intern_pool = intern_pool_init(&arena);
    Namespace *ns = namespace_init(&arena);
    AnalysisState *analysis = analysis_state_init(&arena, intern_pool);

    Buffer file = read_full_file(&arena, test_path);
    if (file.bytes == 0) {
        debug_log_error(STR_LIT("file read failed"));
        goto cleanup;
    }
    FileId file_id = {.id = 1};

    UirLowerState *uir_lower = uir_lower_init(&arena);

    ParserState *parser = parser_init(&arena, file_id, file, intern_pool);
    while (parser_peek_token(parser, 0).type != Token_Eof) {
        AstNodeId top_decl = parser_parse_toplevel(parser);
        ast_print_tree(&parser->ast, intern_pool, top_decl, 0, true);

        AstNode node = ast_get_node(&parser->ast, top_decl);
        switch (node.type) {
            case Ast_VarDecl:
            case Ast_ConstDecl: {
                UirInstId uir_inst = uir_lower_global(uir_lower, file_id, &parser->ast, top_decl);
                Uir uir = uir_lower_emit_chunk(uir_lower);
                uir_print(&uir, intern_pool, uir_inst, 0);

                b32 is_const = node.type == Ast_ConstDecl;
                DeclId decl_id = decl_table_alloc(analysis->decl_table, file_id, node.pos,
                                                  is_const, uir, ns, uir_inst);
                AddNameResult name_res = namespace_add_name(ns, node.decl.name, decl_id);
                if (name_res.redeclared) {
                    // TODO: report global redeclared error
                } else {
                    analysis_queue_decl(analysis, decl_id);
                }
            } break;

            default: {
                debug_assert(false, STR_LIT("only decls are allowed for globals"));
            } break;
        }

        ast_flush(&parser->ast);
    }

    analysis_analyze(analysis);
cleanup:
    if (memory != 0) {
        free(memory);
    }
    return 0;
}
