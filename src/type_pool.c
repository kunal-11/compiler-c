internal TypeBlock
type_pool_alloc_block(TypePool *pool, TypeId *ids, u32 count) {
    debug_assert(pool->extra_count + count <= pool->extra_capacity,
                 STR_LIT("type pool extra OOM"));
    TypeExtraId offset = {.id = pool->extra_count};
    for (u32 i = 0; i < count; i += 1) {
        pool->extras[pool->extra_count + i] = ids[i].id;
    }
    pool->extra_count += count;
    return (TypeBlock){.first = offset, .count = count};
}

internal TypeId
type_pool_get_block_type(TypePool *pool, TypeBlock block, u32 idx) {
    debug_assert(block.first.id + block.count <= pool->extra_count,
                 STR_LIT("type pool extra id OOB"));
    debug_assert(idx < block.count, STR_LIT("type pool arg idx OOB"));
    return TYPE_ID(pool->extras[block.first.id + idx]);
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
            for (u32 i = 0; i < type.fn.args.count; i += 1) {
                TypeId arg_type = type_pool_get_block_type(pool, type.fn.args, i);
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
            if (t1.fn.args.count != t2.fn.args.count) {
                result = false;
                break;
            }
            b32 args_eql = true;
            for (u32 i = 0; i < t1.fn.args.count; i += 1) {
                TypeId arg1 = type_pool_get_block_type(pool, t1.fn.args, i);
                TypeId arg2 = type_pool_get_block_type(pool, t2.fn.args, i);
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

internal Type
type_pool_get_info(TypePool *pool, TypeId id) {
    debug_assert(id.id != Type_None, STR_LIT("type pool get none type"));
    debug_assert(id.id < pool->type_count, STR_LIT("type pool get type OOB"));
    return pool->types[id.id];
}

internal TypeId
type_pool_get_type_id(TypePool *pool, Type type, TypeExtraId extra_marker) {
    TypeId *slot = type_pool_get_slot(pool, type);
    if (slot->id == Type_None) {
        debug_assert(pool->type_count < pool->type_capacity, STR_LIT("type pool OOM"));
        pool->types[pool->type_count] = type;
        slot->id = pool->type_count;
        pool->type_count += 1;
    } else {
        pool->extra_count = extra_marker.id;
    }
    return *slot;
}

internal TypeId
type_pool_fn(TypePool *pool, TypeId *args, u32 arg_count, TypeId ret) {
    TypeBlock args_block = type_pool_alloc_block(pool, args, arg_count);
    Type fn_type = {
        .format = TypeFormat_Fn,
        .fn = {.args = args_block, .ret = ret},
    };
    return type_pool_get_type_id(pool, fn_type, args_block.first);
}

internal TypeId
type_pool_int(TypePool *pool, b8 is_signed, u16 width) {
    Type t = {
        .format = TypeFormat_FixedInt,
        .fixed_int = {.is_signed = is_signed, .width = width},
    };
    return type_pool_get_type_id(pool, t, (TypeExtraId){.id = pool->extra_count});
}
