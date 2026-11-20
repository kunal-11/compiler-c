#include "compiler.c"

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

#define MEM_SIZE (1 * MB)

int
main(void) {
    u8 *memory = (u8 *)malloc(MEM_SIZE);
    if (memory == 0) {
        debug_log_error(STR_LIT("memory alloc failed"));
        goto cleanup;
    }

    Arena arena = {.bytes = memory, .max_len = MEM_SIZE};

    Buffer file = read_full_file(&arena, test_path);
    if (file.bytes == 0) {
        debug_log_error(STR_LIT("file read failed"));
        goto cleanup;
    }
    InternPool *intern_pool = intern_pool_init(&arena);
    FileId file_id = {.id = 1};
    ParserState *parser = parser_init(&arena, file_id, file, intern_pool);

    while (parser_peek_token(parser, 0).type != Token_Eof) {
        AstNodeId top_decl = parser_parse_toplevel(parser);
        ast_print_tree(parser->ast, intern_pool, top_decl, 0, true);
    }
cleanup:
    if (memory != 0) {
        free(memory);
    }
    return 0;
}
