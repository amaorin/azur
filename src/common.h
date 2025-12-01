#include <stdint.h>
#include <intrin.h>

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define S8_MIN  (( s8)0x80)
#define S16_MIN ((s16)0x8000)
#define S32_MIN ((s32)0x80000000)
#define S64_MIN ((s64)0x8000000000000000LL)

#define S8_MAX  (( s8)0x7F)
#define S16_MAX ((s16)0x7FFF)
#define S32_MAX ((s32)0x7FFFFFFF)
#define S64_MAX ((s64)0x7FFFFFFFFFFFFFFFLL)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define U8_MAX  (( u8)0xFF)
#define U16_MAX ((u16)0xFFFF)
#define U32_MAX ((u32)0xFFFFFFFFU)
#define U64_MAX ((u64)0xFFFFFFFFFFFFFFFFULL)

typedef s64 smm;
typedef u64 umm;

#define SMM_MIN S64_MIN
#define SMM_MAX S64_MAX
#define UMM_MAX U64_MAX

typedef u8 bool;
#define true 1
#define false 0

typedef float f32;
typedef double f64;

typedef struct String
{
	u8* data;
	u32 len;
} String;

#define STRING(S) (String){ .data = (u8*)(S), .len = sizeof(S) - 1 }
#define MS_STRING(S) { .data = (u8*)(S), .len = sizeof(S) - 1 }

static bool
String_Equal(String s0, String s1)
{
	bool result = (s0.len == s1.len);

	for (umm i = 0; i < s0.len && result; ++i)
	{
		if (s0.data[i] != s1.data[i])
		{
			result = false;
		}
	}

	return result;
}

#define ASSERT(EX) ((EX) ? 1 : ((*(volatile int*)0 = 0), 0))
#define NOT_IMPLEMENTED ASSERT(!"NOT_IMPLEMENTED")

typedef struct Bump
{
	u8* memory;
	u32 cursor;
	u32 capacity;
	u32 high_watermark;
} Bump;

typedef u32 Bump_Mark;

static void*
Bump_Push(Bump* bump, umm size, u8 alignment)
{
	ASSERT(alignment > 0 && ((alignment-1) & alignment) == 0);

	u64 aligned_cursor = ((u64)bump->cursor + (alignment-1)) & (u64)-(s64)alignment;

	ASSERT(size < U64_MAX - aligned_cursor && aligned_cursor + size <= bump->capacity);

	bump->cursor = (u32)(aligned_cursor + size);
	bump->high_watermark = (bump->cursor > bump->high_watermark ? bump->cursor : bump->high_watermark);

	return &bump->memory[aligned_cursor];
}

static void
Bump_Pop(Bump* bump, umm size)
{
	ASSERT(bump->cursor >= size);
	bump->cursor -= (u32)size;
}

static Bump_Mark
Bump_GetMark(Bump* bump)
{
	return (Bump_Mark)bump->cursor;
}

static void
Bump_PopToMark(Bump* bump, Bump_Mark mark)
{
	ASSERT(mark <= bump->cursor);
	bump->cursor = mark;
}

typedef struct Platform_Link
{
	Bump* frame_bump;
} Platform_Link;

typedef void Game_Tick_Func(Platform_Link* platform_link);
