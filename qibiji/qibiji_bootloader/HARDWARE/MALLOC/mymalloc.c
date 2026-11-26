/* mymalloc：定长块内存池（cJSON 的后端）。
   10KB 池按 32 字节分块，u16 管理表记录每块所属分配的连续块数；
   分配从表末端向前搜索连续空块。全部内部状态 static，对外只暴露三个 API。 */
#include "mymalloc.h"
#include <stddef.h>   /* NULL */

/* ---- 池本体与管理表 ---- */
__align(4) static u8  s_pool[MEM1_MAX_SIZE];              /* 内存池，4 字节对齐（Keil 扩展） */
static u16 s_map[MEM1_ALLOC_TABLE_SIZE];                  /* 管理表：0=空闲，否则=该分配的连续块数 */

/* ---- 内存管理控制器 ---- */
typedef struct {
    u8  ready;        /* 初始化完成标志：0=未就绪（首次分配/释放时自动 init） */
} mem_dev_t;
static mem_dev_t s_dev = { 0 };

/* 设内存 */
static void mymemset(void *s, u8 c, u32 count)
{
    u8 *xs = s;
    while (count--) *xs++ = c;
}

/* 复制内存 */
static void mymemcpy(void *des, void *src, u32 n)
{
    u8 *xdes = des;
    u8 *xsrc = src;
    while (n--) *xdes++ = *xsrc++;
}

/* 初始化：清管理表与池本体，置就绪标志 */
static void mem_init(void)
{
    mymemset(s_map, 0, MEM1_ALLOC_TABLE_SIZE * 2);   /* 表项是 u16，字节数 ×2 */
    mymemset(s_pool, 0, MEM1_MAX_SIZE);
    s_dev.ready = 1;
}

/* 内存分配（内部）：返回块偏移；失败返回 0xFFFFFFFF */
static u32 mem_malloc(u32 size)
{
    signed long offset;
    u16 nmemb;      /* 需要的连续块数 */
    u16 cmemb;      /* 当前连续空块计数 */
    u32 i;

    if (!s_dev.ready) mem_init();          /* 未初始化先初始化（生产路径不显式调 init） */

    if (size == 0) return 0xFFFFFFFF;
    nmemb = size / MEM1_BLOCK_SIZE;
    if (size % MEM1_BLOCK_SIZE) nmemb++;

    for (offset = MEM1_ALLOC_TABLE_SIZE - 1; offset >= 0; offset--)   /* 从池尾向前搜 */
    {
        if (!s_map[offset]) cmemb++;
        else                cmemb = 0;
        if (cmemb == nmemb)                /* 找到连续 nmemb 个空块 */
        {
            for (i = 0; i < nmemb; i++) s_map[offset + i] = nmemb;   /* 逐块标注所属分配 */
            return (u32)(offset * MEM1_BLOCK_SIZE);
        }
    }
    return 0xFFFFFFFF;                     /* 池满 */
}

/* 内存释放（内部）：0=成功 1=未就绪 2=偏移越界 */
static u8 mem_free(u32 offset)
{
    int i, index, nmemb;

    if (!s_dev.ready) { mem_init(); return 1; }

    if (offset < MEM1_MAX_SIZE)
    {
        index = offset / MEM1_BLOCK_SIZE;          /* 偏移所在块号 */
        nmemb = s_map[index];                      /* 该分配的连续块数 */
        for (i = 0; i < nmemb; i++) s_map[index + i] = 0;
        return 0;
    }
    return 2;
}

/* ---- 对外 API ---- */

/* 释放：ptr 必须是 mymalloc 返回的地址；NULL 直接返回 */
void myfree(void *ptr)
{
    u32 offset;
    if (ptr == NULL) return;
    offset = (u32)ptr - (u32)s_pool;
    mem_free(offset);                              /* 返回值丢弃：越界只发生在野指针场景 */
}

/* 分配：返回池内地址；池满/size=0 返回 NULL */
void *mymalloc(u32 size)
{
    u32 offset = mem_malloc(size);
    if (offset == 0xFFFFFFFF) return NULL;
    return (void *)((u32)s_pool + offset);         /* 偏移 + 池基址 = 真实地址 */
}

/* 重分配：新块+拷贝+释放旧块；失败返回 NULL 且旧块保留。
   契约：ptr 非 NULL 且为池内地址
   注意与标准 realloc 的差异：myrealloc(NULL, n) 不成立。 */
void *myrealloc(void *ptr, u32 size)
{
    u32 offset, old_offset, old_bytes, copy_len;
    
    offset = mem_malloc(size);
    if (offset == 0xFFFFFFFF) return NULL;       /* 无足够连续块：旧块原样保留 */

    old_offset = (u32)ptr - (u32)s_pool;                                     /* 旧块起始偏移 */
    old_bytes  = (u32)s_map[old_offset / MEM1_BLOCK_SIZE] * MEM1_BLOCK_SIZE; /* 旧块真实字节数 */
    copy_len   = (size < old_bytes) ? size : old_bytes;                      /* 只搬可读部分，杜绝越界读 */

    mymemcpy((void *)((u32)s_pool + offset), ptr, copy_len);    /* 从 ptr 起把 size 字节逐个搬到新块 */
    myfree(ptr);
    return (void *)((u32)s_pool + offset);
}
