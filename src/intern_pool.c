
internal InternPool *
intern_pool_init(Arena *arena) {
    InternPool *result = arena_push_struct(arena, InternPool);
    for (u32 i = 0; i < INTERN_BUCKET_COUNT; i += 1) {
        result->buckets[i] = INTERN_NULL;
    }
    result->next_id = 1;
    return result;
}

internal InternId
intern_pool_get_id(InternPool *pool, Buffer string) {
    u32 hash = fnv_hash(FNV_INIT, string);
    InternId *slot = &pool->buckets[hash % INTERN_BUCKET_COUNT];
    while (slot->id != INTERN_NULL.id && !buffer_eql(pool->items[slot->id].string, string)) {
        slot = &pool->items[slot->id].next;
    }
    if (slot->id == INTERN_NULL.id) {
        debug_assert(pool->next_id < INTERN_ITEM_COUNT, STR_LIT("intern pool items OOM"));
        slot->id = pool->next_id;
        pool->next_id += 1;
        pool->items[slot->id] = (InternItem){.string = string, .next = INTERN_NULL};
    }
    return *slot;
}

internal Buffer
intern_pool_get_string(InternPool *pool, InternId id) {
    debug_assert(id.id != INTERN_NULL.id, STR_LIT("intern pool get null id"));
    return pool->items[id.id].string;
}
