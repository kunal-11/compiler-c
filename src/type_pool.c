typedef enum {
    Type_None,

    Type_NoReturn,
    Type_Void,
    Type_Bool,
    Type_TypeId,

    Type_CompInt,
    Type_CompFloat,

    Type_Usize,
    Type_Isize,

    Type_FirstUserDef,
} TypeIndex;

typedef struct {
    u32 id;
} TypeId;

#define TYPE_ID(TypeIndex) ((TypeId){.id = (TypeIndex)})

typedef struct {
    u32 id;
} TypeExtraId;

typedef enum {
    TypeFormat_Invalid,

    TypeFormat_Global,
    TypeFormat_FixedInt,
    TypeFormat_Float,
    TypeFormat_Ptr,
    TypeFormat_Fn,
} TypeFormat;

typedef struct {
    b8 is_signed;
    u16 width;
} FixedInt;

typedef enum {
    Float_32,
    Float_64,
} FloatFormat;

typedef struct {
    TypeId to;
} PtrInfo;

typedef struct {
    TypeExtraId first_arg;
    u32 arg_count;
    TypeId ret;
} FnTypeInfo;

typedef struct {
    TypeFormat format;
    union {
        TypeId global;
        FixedInt fixed_int;
        FloatFormat float_format;
        PtrInfo ptr_info;
        FnTypeInfo fn;
    };
    TypeId next_in_hash;
} Type;

typedef struct {
    TypeId *buckets;
    u32 bucket_count;

    Type *types;
    u32 type_count;
    u32 type_capacity;

    u32 *extras;
    u32 extra_count;
    u32 extra_capacity;
} TypePool;

internal TypeId
type_pool_fn_arg(TypePool *pool, FnTypeInfo fn_type, u32 idx) {
    debug_assert(fn_type.first_arg.id + fn_type.arg_count <= pool->extra_count,
                 STR_LIT("type pool extra id OOB"));
    debug_assert(idx < fn_type.arg_count, STR_LIT("type pool arg idx OOB"));
    return TYPE_ID(pool->extras[fn_type.first_arg.id + idx]);
}

internal u32
type_hash(TypePool *pool, Type type) {
    u32 hash = fnv_hash(FNV_INIT, to_buffer(type.format));
    switch (type.format) {
        case TypeFormat_Global: {
            hash = fnv_hash(hash, to_buffer(type.global));
        } break;

        case TypeFormat_FixedInt: {
            hash = fnv_hash(hash, to_buffer(type.fixed_int.is_signed));
            hash = fnv_hash(hash, to_buffer(type.fixed_int.width));
        } break;

        case TypeFormat_Float: {
            hash = fnv_hash(hash, to_buffer(type.float_format));
        } break;

        case TypeFormat_Ptr: {
            hash = fnv_hash(hash, to_buffer(type.ptr_info.to.id));
        } break;

        case TypeFormat_Fn: {
            for (u32 i = 0; i < type.fn.arg_count; i += 1) {
                TypeId arg_type = type_pool_fn_arg(pool, type.fn, i);
                hash = fnv_hash(hash, to_buffer(arg_type));
            }
            hash = fnv_hash(hash, to_buffer(type.fn.ret));
        } break;

        case TypeFormat_Invalid: {
            UNREACHABLE;
        } break;
    }
    return hash;
}

internal b32
type_eql(TypePool *pool, Type t1, Type t2) {
    if (t1.format != t2.format) {
        return false;
    }
    b32 result = false;
    switch (t1.format) {
        case TypeFormat_Global: {
            result = t1.global.id == t2.global.id;
        } break;

        case TypeFormat_FixedInt: {
            result = t1.fixed_int.is_signed == t2.fixed_int.is_signed &&
                     t1.fixed_int.width == t2.fixed_int.width;
        } break;

        case TypeFormat_Float: {
            result = t1.float_format == t2.float_format;
        } break;

        case TypeFormat_Ptr: {
            result = t1.ptr_info.to.id == t2.ptr_info.to.id;
        } break;

        case TypeFormat_Fn: {
            if (t1.fn.arg_count != t2.fn.arg_count) {
                result = false;
                break;
            }
            b32 args_eql = true;
            for (u32 i = 0; i < t1.fn.arg_count; i += 1) {
                TypeId arg1 = type_pool_fn_arg(pool, t1.fn, i);
                TypeId arg2 = type_pool_fn_arg(pool, t2.fn, i);
                if (arg1.id != arg2.id) {
                    args_eql = false;
                    break;
                }
            }
            result = (t1.fn.ret.id == t2.fn.ret.id) && args_eql;
        } break;

        case TypeFormat_Invalid: {
            UNREACHABLE;
        } break;
    }
    return result;
}

#define TYPE_POOL_BUCKETS 4096
#define TYPE_POOL_CAPACITY 8192
#define TYPE_POOL_EXTRA_CAPACITY 4096

internal TypeId *
type_pool_get_slot(TypePool *pool, Type type) {
    TypeId *result = &pool->buckets[type_hash(pool, type) % pool->bucket_count];
    while (result->id != Type_None) {
        debug_assert(result->id < pool->type_count, STR_LIT("type pool id OOB"));
        if (type_eql(pool, type, pool->types[result->id])) {
            break;
        }
        result = &pool->types[result->id].next_in_hash;
    }
    return result;
}

internal TypePool *
type_pool_init(Arena *arena) {
    TypePool *pool = arena_push_struct(arena, TypePool);
    pool->buckets = arena_push_array(arena, TypeId, TYPE_POOL_BUCKETS);
    pool->bucket_count = TYPE_POOL_BUCKETS;

    pool->types = arena_push_array(arena, Type, TYPE_POOL_CAPACITY);
    pool->type_count = Type_FirstUserDef;
    pool->type_capacity = TYPE_POOL_CAPACITY;

    pool->extras = arena_push_array(arena, u32, TYPE_POOL_EXTRA_CAPACITY);
    pool->extra_count = 0;
    pool->extra_capacity = TYPE_POOL_EXTRA_CAPACITY;

    for (u32 i = 0; i < pool->bucket_count; i += 1) {
        pool->buckets[i] = TYPE_ID(Type_None);
    }

    pool->types[0] = (Type){
        .format = TypeFormat_Invalid,
        .next_in_hash = TYPE_ID(Type_None),
    };
    for (u32 i = 1; i < Type_FirstUserDef; i += 1) {
        pool->types[i] = (Type){
            .format = TypeFormat_Global,
            .global = TYPE_ID(i),
            .next_in_hash = TYPE_ID(Type_None),
        };
        TypeId *slot = type_pool_get_slot(pool, pool->types[i]);
        *slot = TYPE_ID(i);
    }
    return pool;
}
