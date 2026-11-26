#include "compiler.c"
#include "debug_printer.c"

#include <stdio.h>
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
    TypePool *type_pool = type_pool_init(&arena);
    DeclTable *decl_table = decl_table_init(&arena);
    FnTable *fn_table = fn_table_init(&arena);

    Namespace *ns = namespace_init(&arena);
    // AnalysisState *analysis = analysis_state_init(&arena, intern_pool, decl_table);

    Buffer file = read_full_file(&arena, test_path);
    if (file.bytes == 0) {
        debug_log_error(STR_LIT("file read failed"));
        goto cleanup;
    }
    FileId file_id = {.id = 1};

    ParserState *parser = parser_init(&arena, file_id, file, intern_pool);
    while (parser_peek_token(parser, 0).type != Token_Eof) {
        AstNodeId top_decl = parser_parse_toplevel(parser);
        ast_print_tree(parser->ast, intern_pool, top_decl, 0, true);

        UirLowerState *uir_lower = uir_lower_init(&arena, parser->ast, file_id, file);
        UirInstId uir_inst = uir_lower_global(uir_lower, top_decl);
        uir_print(uir_lower->uir, intern_pool, uir_inst, 0);

        EvalState *eval = eval_state_init(&arena, intern_pool, type_pool, decl_table, fn_table, ns,
                                          uir_lower->uir, file_id);
        EvalValue value = eval_inst(eval, uir_inst, EvalMode_Exec);

        printf("\nEval:\n");
        for (u32 i = 2; i < eval->uir->next_inst; i += 1) {
            printf("%%%d: ", i);
            eval_print_value(type_pool, eval->values[i]);
            printf("\n");
        }

        printf("\nResult: ");
        eval_print_value(type_pool, value);
        printf("\n\n");
        // DeclId decl_id = decl_table_alloc(decl_table, file_id, uir_lower->uir, uir_inst);
        // analysis_queue_decl(analysis, decl_id);
    }
    // while (analysis_process_entry(analysis)) {
    // }
cleanup:
    if (memory != 0) {
        free(memory);
    }
    return 0;
}
