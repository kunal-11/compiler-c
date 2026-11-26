#include "type_pool.c"
#include "eval.c"

typedef enum {
    AnalysisType_Decl,
    AnalysisType_Fn,
} AnalysisType;

typedef struct {
    AnalysisType type;
    union {
        DeclId decl;
    };
} AnalysisEntry;

typedef struct {
    AnalysisEntry *entries;
    u32 capacity;

    u32 read_idx;
    u32 queued_count;
} AnalysisQueue;

internal void
analysis_queue_push_entry(AnalysisQueue *queue, AnalysisEntry entry) {
    debug_assert(queue->queued_count < queue->capacity, STR_LIT("analysis queue OOM"));
    queue->entries[(queue->read_idx + queue->queued_count) % queue->capacity] = entry;
    queue->queued_count += 1;
}

internal AnalysisEntry
analysis_queue_pop_entry(AnalysisQueue *queue) {
    debug_assert(queue->queued_count > 0, STR_LIT("analysis pop on empty queue"));
    AnalysisEntry entry = queue->entries[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % queue->capacity;
    queue->queued_count -= 1;
    return entry;
}

typedef struct {
    InternPool *intern_pool;
    DeclTable *decl_table;
    TypePool *type_pool;

    AnalysisQueue queue;
} AnalysisState;

#define ANALYSIS_QUEUE_CAPACITY 2048

internal AnalysisState *
analysis_state_init(Arena *arena, InternPool *intern_pool, DeclTable *decl_table) {
    AnalysisState *state = arena_push_struct(arena, AnalysisState);
    state->queue = (AnalysisQueue){
        .entries = arena_push_array(arena, AnalysisEntry, ANALYSIS_QUEUE_CAPACITY),
        .capacity = ANALYSIS_QUEUE_CAPACITY,
        .read_idx = 0,
        .queued_count = 0,
    };
    state->type_pool = type_pool_init(arena);
    state->intern_pool = intern_pool;
    state->decl_table = decl_table;
    return state;
}

internal void
analysis_queue_decl(AnalysisState *state, DeclId decl_id) {
    AnalysisEntry entry = {
        .type = AnalysisType_Decl,
        .decl = decl_id,
    };
    analysis_queue_push_entry(&state->queue, entry);

    Decl *decl = decl_table_get_decl(state->decl_table, decl_id);
    decl->status = DeclStatus_Queued;
}

internal b32
analysis_process_entry(AnalysisState *state) {
    if (state->queue.queued_count == 0) {
        return false;
    }
    AnalysisEntry entry = analysis_queue_pop_entry(&state->queue);
    switch (entry.type) {
        case AnalysisType_Decl: {
            // Decl *decl = decl_table_get_decl(state->decl_table, entry.decl);
            printf("analysis decl: %d\n", entry.decl.id);
        } break;

        case AnalysisType_Fn: {
            UNREACHABLE;
        } break;
    }
    return true;
}
