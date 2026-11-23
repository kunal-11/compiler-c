#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef int8_t i8;

typedef uint16_t u16;
typedef int16_t i16;

typedef uint32_t u32;
typedef int32_t i32;

typedef uint64_t u64;
typedef int64_t i64;

typedef size_t usize;
typedef ptrdiff_t isize;

typedef float f32;
typedef double f64;

typedef u32 b32;
typedef u8 b8;

#define true 1
#define false 0

#define internal static
#define local_persist static
#define global_variable static

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    u8 *bytes;
    usize len;
} Buffer;

#define STR_LIT(s) ((Buffer){.bytes = ((u8 *)s), .len = sizeof(s) - 1})

// debug stuff
#include <stdio.h>
internal void
debug_log_info(Buffer str) {
    printf("info: %.*s\n", (int)str.len, str.bytes);
}

internal void
debug_log_error(Buffer str) {
    printf("error: %.*s\n", (int)str.len, str.bytes);
}

#define UNREACHABLE __builtin_debugtrap()

internal void
debug_assert(b32 ok, Buffer msg) {
    if (!ok) {
        debug_log_error(msg);
        UNREACHABLE;
    }
}

// mem stuff
internal Buffer
buffer_slice(Buffer buf, usize start, usize end) {
    debug_assert(start <= end, STR_LIT("slice start not greater than end"));
    debug_assert(end <= buf.len, STR_LIT("slice end greater than buffer size"));
    return (Buffer){.bytes = buf.bytes + start, .len = end - start};
}

internal b32
buffer_eql(Buffer buf1, Buffer buf2) {
    if (buf1.len != buf2.len)
        return false;
    for (usize i = 0; i < buf1.len; i += 1) {
        if (buf1.bytes[i] != buf2.bytes[i])
            return false;
    }
    return true;
}

#define to_buffer(val) ((Buffer){.bytes = (u8 *)&(val), .len = sizeof(val)})

#define KB (1 << 10)
#define MB (1 << 20)
#define GB (1 << 30)

internal usize
align_down(usize val, usize align) {
    debug_assert(align > 0 && (align & (align - 1)) == 0, STR_LIT("alignment not power of 2"));
    return (val & ~(align - 1));
}

internal usize
align_up(usize val, usize align) {
    return align_down(val + align - 1, align);
}

internal usize
checked_add(usize v1, usize v2) {
    usize res;
    if (__builtin_add_overflow(v1, v2, &res)) {
        debug_assert(false, STR_LIT("checked add overflow"));
    }
    return res;
}

typedef struct {
    u8 *bytes;
    usize used;
    usize max_len;
} Arena;

internal void *
arena_peek(Arena *arena, usize align) {
    usize ptr = (usize)arena->bytes + arena->used;
    return (void *)align_up(ptr, align);
}

internal Buffer
arena_push_bytes(Arena *arena, usize len, usize align) {
    usize ptr = (usize)arena->bytes + arena->used;
    usize align_offset = align_up(ptr, align) - ptr;
    usize aligned_len = checked_add(align_offset, len);

    debug_assert(aligned_len <= arena->max_len - arena->used, STR_LIT("arena OOM"));
    arena->used += aligned_len;
    return (Buffer){
        .bytes = arena->bytes + (arena->used - len),
        .len = len,
    };
}

#define arena_push_struct(arena, Type)                                                            \
    ((Type *)arena_push_bytes((arena), sizeof(Type), __alignof__(Type)).bytes)

#define arena_push_array(arena, Type, len)                                                        \
    ((Type *)arena_push_bytes((arena), sizeof(Type) * (len), __alignof__(Type)).bytes)

#define arena_peek_struct(arena, Type) ((Type *)arena_peek(arena, __alignof__(Type)))

#define arena_list_count(arena, start)                                                            \
    (((arena)->used - (usize)((u8 *)(start) - (arena)->bytes)) / sizeof(*(start)))

internal Arena
sub_arena(Arena *arena, usize size) {
    u8 *bytes = arena_push_array(arena, u8, size);
    return (Arena){.bytes = bytes, .used = 0, .max_len = size};
}

typedef struct {
    Arena *arena;
    usize used;
} Flusher;

internal Flusher
flusher_init(Arena *arena) {
    return (Flusher){.arena = arena, .used = arena->used};
}

internal void
flusher_flush(Flusher flusher) {
    flusher.arena->used = flusher.used;
}

// math stuff
#define FNV_INIT 0x811C9DC5
#define FNV_PRIME 0x01000193

internal u32
fnv_hash(u32 init, Buffer buf) {
    for (usize i = 0; i < buf.len; i += 1) {
        init ^= buf.bytes[i];
        init *= FNV_PRIME;
    }
    return init;
}
