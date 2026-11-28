typedef enum {
    Type_None,

    Type_NoReturn,
    Type_Void,
    Type_Bool,
    Type_TypeId,

    Type_CompInt,
    Type_CompFloat,
    Type_CompUndef,

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
    TypeExtraId first;
    u32 count;
} TypeBlock;

typedef struct {
    TypeBlock args;
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

internal TypePool *
type_pool_init(Arena *arena);

internal Type
type_pool_get_info(TypePool *pool, TypeId id);

internal TypeId
type_pool_fn(TypePool *pool, TypeId *args, u32 arg_count, TypeId ret);

internal TypeId
type_pool_int(TypePool *pool, b8 is_signed, u16 width);
