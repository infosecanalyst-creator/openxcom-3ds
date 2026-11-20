// Heap set-up for the 3DS (replaces libctru's __system_allocateHeaps), OX-06.
// Shared pattern from the SimGolf port (opensgolf-3ds heap3ds.c), from real hardware:
//  - wait up to 3 s for the previous app's memory to be released before sizing the heaps
//    (libctru's default takes whatever is free at that instant, which can be short);
//  - leave 4 MB uncommitted. With every byte committed (libctru's default), the next
//    program the Homebrew loader starts after this one quits can fail to load.
// Sizes: linear heap 32 MB as before (libctru's default; SDL surfaces, audio buffers);
// the main heap gets the rest minus the reserve (~70 MB on a New 3DS, the game's
// Battlescape needs ~50-60 MB, README).
#include <3ds.h>
#include <3ds/allocator/mappable.h>

extern "C" {
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

void __system_allocateHeaps(void)
{
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    for (int tries = 0; tries < 300; tries++) {
        svcGetResourceLimitLimitValues(&maxCommit, reslimit, &type, 1);
        svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &type, 1);
        if (maxCommit - currentCommit >= (88ll << 20)) break;   // a New 3DS with the last app's memory back
        svcSleepThread(10000000LL);
    }
    svcCloseHandle(reslimit);

    const u32 reserve = 4u << 20;
    u32 remaining = (u32)(maxCommit - currentCommit) & ~0xFFFu;
    remaining = remaining > reserve * 4 ? remaining - reserve : remaining;
    const u32 heap_max = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
    const u32 linear_want = 32u << 20;
    u32 heap = remaining > linear_want ? remaining - linear_want : remaining / 2;
    if (heap > heap_max) heap = heap_max;
    heap &= ~0xFFFu;
    __ctru_heap_size = heap;
    __ctru_linear_heap_size = remaining - heap;

    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR,
                                  (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}
