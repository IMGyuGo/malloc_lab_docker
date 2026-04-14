/*
 * mm-naive.c - 가장 빠르지만 메모리 효율이 가장 낮은 malloc 패키지.
 *
 * 이 단순한 방식에서는 블록을 brk 포인터를 단순히 증가시켜 할당한다.
 * 블록은 순수 페이로드만 있고 헤더/푸터가 없다. 블록은 합병되거나
 * 재사용되지 않는다. realloc은 mm_malloc과 mm_free를 직접 사용해 구현한다.
 *
 * 학생 안내: 이 헤더 주석을 본인의 malloc 설계를 요약하는 주석으로 바꾸시오.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 학생 안내: 다른 작업 전에 아래 struct에 팀 정보를 적으시오.
 ********************************************************/
team_t team = {
    /* 팀 이름 */
    "6",
    /* 1번 멤버 전체 이름 */
    "Kim Gyu Min",
    /* 1번 멤버 이메일 */
    "gyugo4894@gmail.com",
    /* 2번 멤버 전체 이름 (없으면 빈 문자열) */
    "",
    /* 2번 멤버 이메일 (없으면 빈 문자열) */
    ""};

/* 단일 워드(4) 또는 더블 워드(8) 정렬 */
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 10) //(1 << 12)
#define ALIGNMENT 8

/* 크기를 ALIGNMENT의 배수로 올림 */
/*
예를 들어, 15byte를 할당 시,
(15 + 7)
10110 & 11111000 = 10000 -> (16)

예를 들어, 16byte를 할당 시,
(16 + 7)
10111 & 11111000 = 10000 -> (16)

예를 들어, 17byte를 할당 시,
(17 + 7)
11000 & 11111000 = 11000 -> (24)

즉, 올림처리
*/
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MINBLOCKSIZE (DSIZE + (2 * SIZE_T_SIZE))
#define SEG_LIST_COUNT 16
#define SEG_ROOT_BYTES (SEG_LIST_COUNT * SIZE_T_SIZE)
#define SEG_MAX_INDEX (SEG_LIST_COUNT - 1)
#define SEG_INDEX(size)          \
    ((size) <= MINBLOCKSIZE ? 0  \
     : (size) <= 1 << 5     ? 1  \
     : (size) <= 1 << 6     ? 2  \
     : (size) <= 1 << 7     ? 3  \
     : (size) <= 1 << 8     ? 4  \
     : (size) <= 1 << 9     ? 5  \
     : (size) <= 1 << 10    ? 6  \
     : (size) <= 1 << 11    ? 7  \
     : (size) <= 1 << 12    ? 8  \
     : (size) <= 1 << 13    ? 9  \
     : (size) <= 1 << 14    ? 10 \
     : (size) <= 1 << 15    ? 11 \
     : (size) <= 1 << 16    ? 12 \
     : (size) <= 1 << 17    ? 13 \
     : (size) <= 1 << 18    ? 14 \
                            : 15)

/**
 * 사이즈를 지정할 때, 왜 size_t로 8바이트 데이터 타입을 쓰는지 생각해보니
 * 메모리 용량이 4바이트일 경우 2^32 = 2^10 = 1000byte -> (1000)^3 * 2^2 = 4 * 10^9 = 4,000,000kB = 4,000Mb = 4Gbyte
 * 메모리 용량이 4기가 보다 더 클 수도 있기 때문에 (나의 생각)
 */
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET2(p) (*(void **)(p))              // 포인터 값을 조회하기 위한 용도
#define PUT2(p, val) (*(void **)(p) = (val)) // 포인터 값을 저장하기 위한 용도
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
// 여기서 footer를 안 쓰면, footer에 정보를 넣을 수 가 없다.
#define HDRP(bp) ((char *)(bp) - WSIZE)
/**
 * 내 생각
 * 4바이트씩 PRED, SUCC 할당하면 8바이트만 늘리면 되고, 4바이트 안에 주소가 다 넣을 정도의 예시들만 있을 것 같다.
 * 원래 64bit 컴퓨터라면 8바이트를 할당해야 하긴 한다.
 * -> 물어보니
 * 무조건 포인터 크기는 플랫폼에 따라 고정되므로
 * 할당하는 데이터 크기 (4바이트냐 8바이트냐) 상관없이 주소를 저장하려면 포인터 크기만큼 무조건 필요
 */
#define PRED(bp) ((char *)(bp))               // 현재 bp의 PRED가 저장되어 있는 위치
#define SUCC(bp) ((char *)(bp) + SIZE_T_SIZE) // 현재 bp의 SUCC가 저장되어 있는 위치
#define GET_PRED(bp) ((char *)GET2(PRED(bp)))
#define GET_SUCC(bp) ((char *)GET2(SUCC(bp)))
#define SET_PRED(bp, ptr) (PUT2(PRED(bp), (ptr)))
#define SET_SUCC(bp, ptr) (PUT2(SUCC(bp), (ptr)))
#define SEG_HEAD(index) ((char *)(seg_free_listp + ((index) * SIZE_T_SIZE)))
#define GET_SEG_HEAD(index) ((char *)GET2(SEG_HEAD(index)))
#define SET_SEG_HEAD(index, ptr) (PUT2(SEG_HEAD(index), (ptr)))
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static char *heap_listp;
// 가용 리스트에 제일 앞에 담기는 것
// 정해진 리스트의 개수를 배열로 담는 그릇
static char *seg_free_listp;

/*
 * SEG LIST 전환 힌트
 * - 지금은 `heap_free_listp` 하나만 쓰는 단일 explicit free list 구조다.
 * - 다음 단계에서는 `static char *seg_free_listp[SEG_LIST_COUNT];` 같은 head 배열로 바꾸는 쪽이 가장 단순하다.
 * - `SEG_INDEX(size)`는 "이 크기의 free block이 어느 리스트로 들어가야 하는가"를 빠르게 정하는 용도다.
 *
 * 함수 추가/수정 힌트
 * - `mm_init`
 *   모든 seglist head를 NULL로 초기화해야 한다.
 *   만약 head 배열을 힙 앞부분에 둘 생각이면 `SEG_ROOT_BYTES`만큼 prologue 전에 추가 공간을 확보하면 된다.
 *
 * - `push_free`
 *   이제는 단일 head가 아니라 `SEG_INDEX(GET_SIZE(HDRP(bp)))`로 class를 구해서
 *   해당 class head에만 LIFO 방식으로 insert하면 된다.
 *
 * - `rem_free`
 *   제거할 블록의 현재 크기로 class를 다시 구한 뒤,
 *   그 class list 안에서만 pred/succ를 끊어주면 된다.
 *
 * - `find_fit`
 *   요청 크기의 class에서 시작해서 더 큰 class로 올라가며 탐색하면 된다.
 *   처음에는 "각 class 내부 first fit + 상위 class 순차 탐색"만 해도 구현이 단순하고 충분히 빠르다.
 *
 * - `coalesce`
 *   이웃 free block이 있으면 각각 자기 class list에서 먼저 제거한 뒤 합친다.
 *   그리고 최종 병합 블록 한 개만 새 크기에 맞는 class로 다시 insert해야 한다.
 *
 * - `place` / `place_allocated`
 *   할당 전에 원래 free block은 해당 seglist에서 제거해야 한다.
 *   split이 생기면 남은 free block은 "남은 크기 기준 class"로 다시 insert해야 한다.
 *
 * - 필요하면 나중에 `find_fit` 안에서만 쓸 작은 helper를 하나 더 만들어도 된다.
 *   예: "특정 class head를 가져오는 함수", "다음 non-empty class를 찾는 함수"
 */

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static char *find_fit(size_t size);
static void place(char *bp, size_t asize);
static void place_allocated(char *bp, size_t block_size, size_t asize, int coalesce_split);
static void push_free(void *p);
static void rem_free(void *p);

/*
 * mm_init - malloc 패키지 초기화.
 */
int mm_init(void)
{
    // (void*) -1
    /**
     * (void *)는 모든 타입을 저장할 수 있는 저장소 (주소만 알 수 있음)
     * 즉, 어떤 타입이든 담을 수 있는 generic pointer
     * 주소만 가지고 있고, 역참조하려면 캐스팅 필요
     *
     * (void*) -1 (유효하지 않은 주소)
     * 보통 시스템 콜 실패 시 반환값!
     */
    size_t prologue_size = SEG_ROOT_BYTES + DSIZE;

    if ((heap_listp = mem_sbrk(SEG_ROOT_BYTES + (4 * WSIZE))) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); // 4 byte 패딩 설정
    PUT(heap_listp + (1 * WSIZE), PACK(prologue_size, 1));

    seg_free_listp = heap_listp + (2 * WSIZE);
    for (int i = 0; i < SEG_LIST_COUNT; i++)
    {
        SET_SEG_HEAD(i, NULL);
    }
    PUT(heap_listp + SEG_ROOT_BYTES + (2 * WSIZE), PACK(prologue_size, 1));
    PUT(heap_listp + SEG_ROOT_BYTES + (3 * WSIZE), PACK(0, 1));

    heap_listp = seg_free_listp;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
    {
        return NULL;
    }

    // 이거 때문에 초기화 할 때, 패딩과 프롤로그 헤더를 따로 만든다.
    PUT(HDRP(bp), PACK(size, 0));         // free block header
    PUT(FTRP(bp), PACK(size, 0));         // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header

    // 힙을 늘릴 때, 앞에 있는 것이 Free일 수 도 있기 때문
    // 힙 확장 전략을 고려한 것 (My Think 검증 아직 안함)
    return coalesce(bp);
}

// heap_free_listp에 스택 방식으로 집어넣기
static void push_free(void *p)
{
    if (p == NULL)
    {
        return;
    }

    // 1. 사이즈를 구한다.
    size_t size = GET_SIZE(HDRP(p));

    // 2. 사이즈에 맞는 클래스를 찾는다.
    char index = SEG_INDEX(size);

    // 3. LIFO 방식으로 집어넣는다.
    char *old_ptr;

    old_ptr = GET_SEG_HEAD(index);

    /**
     * PUT2(PRED(p), NULL);
     * PUT2(SUCC(p), old_ptr);
     */
    SET_PRED(p, NULL);
    SET_SUCC(p, old_ptr);
    if (old_ptr != NULL)
    {
        SET_PRED(old_ptr, p);
    }
    SET_SEG_HEAD(index, p);
}

// heap_free_listp에 스택 방식으로 빼기
// 만약에 rem_free할 공간을 찾질 못한다면?
static void rem_free(void *p)
{
    // 1. 먼저 사이즈를 구한다.
    if (p == NULL)
    {
        return;
    }

    size_t size = GET_SIZE(HDRP(p));
    // 2. 사이즈에 맞는 클래스를 찾는다.
    char index = SEG_INDEX(size);
    // 3. LIFO 방식으로 클래스에서 찾아서 제거한다.
    // -> 하지만 그 클래스에 아무것도 존재하지 않는다면
    // -> 다음 블럭으로 넘어가서 체크를 해봐야 한다.

    // 중요!!
    if (index < SEG_LIST_COUNT)
    {
        char *pred = GET_PRED(p);
        char *succ = GET_SUCC(p);

        /**
         * 만약 없어진게 첫번째꺼라면
         * free list 첫번째에 값 집어넣기
         **/
        if (pred == NULL)
        {
            SET_SEG_HEAD(index, succ);
        }
        else
        {
            // 앞에 있는거의 뒤를 다음걸로 설정
            SET_SUCC(pred, succ);
        }

        // 만약 마지막이 아니라면,
        if (succ != NULL)
        {
            // 뒤에 있는거의 앞을 이전걸로 설정
            SET_PRED(succ, pred);
        }

        // 중요!!
        SET_PRED(p, NULL);
        SET_SUCC(p, NULL);

        return;
    }

    return;
}

static void *coalesce(void *bp)
{
    char *prev_bp = PREV_BLKP(bp);
    char *next_bp = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(FTRP(prev_bp));
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t size = GET_SIZE(HDRP(bp));

    // free 전략을 취할 때,
    // 합치면서 heap_free_listp에 있는거 제거 후 다시 추가
    // 여기서 나는 계속 rem_free(bp)를 해줬었는데,
    // 아직 bp는 생각해보니 heap_free_listp에 들어가지 않은 상태였다.. 할필요가 없었다.. (crash 발생 원인..ㅜㅜ)
    if (!prev_alloc)
    {
        rem_free(prev_bp);
        size += GET_SIZE(HDRP(prev_bp));
        bp = prev_bp;
    }

    if (!next_alloc)
    {
        rem_free(next_bp);
        size += GET_SIZE(HDRP(next_bp));
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    push_free(bp);
    return bp;
}

static void place_allocated(char *bp, size_t block_size, size_t asize, int coalesce_split)
{
    size_t remain_size = block_size - asize;
    char *split_ptr;

    if (remain_size < MINBLOCKSIZE)
    {
        PUT(HDRP(bp), PACK(block_size, 1));
        PUT(FTRP(bp), PACK(block_size, 1));
        return;
    }

    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));

    split_ptr = NEXT_BLKP(bp);
    PUT(HDRP(split_ptr), PACK(remain_size, 0));
    PUT(FTRP(split_ptr), PACK(remain_size, 0));

    // realloc처럼 allocated block 중간에서 split한 free block은 다음 블록과 바로 이어질 수 있으므로 coalesce 경로가 필요하다.
    // mm_malloc에서 원래 free block을 잘라 쓰는 경우에는 이미 coalescing이 끝난 상태라 push_free만 해도 된다.
    if (coalesce_split)
    {
        coalesce(split_ptr);
    }
    else
    {
        push_free(split_ptr);
    }
}

/*
 * mm_malloc - brk 포인터를 늘려 블록 할당.
 *     항상 정렬 배수 크기의 블록을 할당한다.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;

    /**
     * explicit free list의 경우
     * pred, succ가 8바이트씩 더 잡는다고 생각해서 무조건 32byte가 최소단위라고 생각했는데,
     * 아니고 기존 payload 위치를 pred, succ가 대체한다고 생각해야함..
     *
     * size의 경우는 예제에 따라 4byte로 넣어도 상관없지만
     * 주소는 64bit의 컴퓨터의 경우 환경에 따라 8byte로 주소가 들어갈 수 있으므로
     *
     * MINBLOCKSIZE = 24 byte
     */
    asize = MAX(ALIGN(size + DSIZE), MINBLOCKSIZE);

    // 할당 하기 전에 영역 전부를 다 살펴보고
    // place를 통해 그 자리에 할당
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

#define SMALL_FIT_CUTOFF 128
#define IS_WILDERNESS(bp) (GET_SIZE(HDRP(NEXT_BLKP(bp))) == 0)

static char *find_fit(size_t size)
{
    int index = SEG_INDEX(size);
    char *saved_top = NULL;
    size_t saved_top_size = 0;

    for (int i = index; i < SEG_LIST_COUNT; i++)
    {
        char *bp = GET_SEG_HEAD(i);
        char *best_bp = NULL;
        size_t best_size = 0;

        while (bp != NULL)
        {
            size_t free_size = GET_SIZE(HDRP(bp));

            if (free_size >= size)
            {
                if (size <= SMALL_FIT_CUTOFF && IS_WILDERNESS(bp))
                {
                    if (saved_top == NULL || free_size < saved_top_size)
                    {
                        saved_top = bp;
                        saved_top_size = free_size;
                    }
                    bp = GET_SUCC(bp);
                    continue;
                }

                if (i == index)
                {
                    return bp; // same-bin first-fit
                }

                if (best_bp == NULL || free_size < best_size)
                {
                    best_bp = bp;
                    best_size = free_size;
                }
            }
            bp = GET_SUCC(bp);
        }

        if (best_bp != NULL)
        {
            return best_bp;
        }
    }

    return saved_top; // 다른 후보가 없을 때만 wilderness 사용
}

static void place(char *p, size_t asize)
{
    // 실제 사이즈
    size_t real_size = GET_SIZE(HDRP(p));

    rem_free(p);

    place_allocated(p, real_size, asize, 0);
}

/*
 * mm_free - 블록 해제 시 아무 동작도 하지 않음.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    // *(size_t *)ptr = 0;
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

void *mm_realloc(void *ptr, size_t size)
{
    // free alloc free
    // 1. 사이즈가 감소할 경우 -> place로 자르기
    // 2. 뒤에 free가 있고, 뒤에 사이즈를 더한 값이 size보다 클 경우에는 딱 충분한 크기만큼만 확장 (free일 경우) - place를 사용해서 구현
    // 3. 뒤가 확장이 불가능한 eplilogue header일 경우
    // 4. 이 경우들 다 빼고 예외상황에는 단순구현으로

    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    void *newptr;
    char *next_bp = NEXT_BLKP(ptr);
    size_t asize = MAX(ALIGN(size + DSIZE), MINBLOCKSIZE); // 요청크기
    size_t cur_size = GET_SIZE(HDRP(ptr));
    size_t next_size = GET_SIZE((HDRP(next_bp)));
    size_t copySize;

    // 1. 요청 크기가 더 작아진 경우에는 같은 자리에서 바로 잘라서 남는 부분만 free block으로 돌려준다.
    if (asize <= cur_size)
    {
        // 이걸 하는게 더 메모리 효율이 좋을까? 나쁠까? 분석
        // place_allocated(ptr, cur_size, asize, 1);
        return ptr;
    }

    // 1-1. malloc으로 공간있는지 먼저 확인
    // realloc에서는 현재 allocated block 자체에 coalesce(ptr)를 직접 쓰면 안 된다.

    // 2. 확장가능 alloc free
    // 이전 free block과 합쳐지면 payload 시작 주소가 바뀔 수 있기 때문이다.
    // 따라서 바로 뒤 free block만 free list에서 제거해서 현재 블록 뒤에 직접 붙인다.
    if (!GET_ALLOC(HDRP(next_bp)) && (cur_size + next_size) >= asize)
    {

        rem_free(next_bp);
        place_allocated(ptr, cur_size + next_size, asize, 1);
        return ptr;
    }

    // 바로 뒤가 epilogue면 heap을 늘린 뒤, 다시 한 번 "뒤 free 붙이기"를 시도한다.
    // 공간이 없으므로 추가
    // [alloc epilogue]으로, 아예 heap 자체를 확장해야하는 경우 -> [alloc free epilogue]
    if (next_size == 0 || (!GET_ALLOC(HDRP(next_bp)) && (cur_size + next_size) < asize))
    {
        // MAX(asize - cur_size, CHUNKSIZE)
        if (extend_heap((asize - cur_size) / WSIZE) != NULL)
        {
            next_bp = NEXT_BLKP(ptr);
            next_size = GET_SIZE(HDRP(next_bp));

            if (!GET_ALLOC(HDRP(next_bp)) && (cur_size + next_size) >= asize)
            {
                rem_free(next_bp);
                place_allocated(ptr, cur_size + next_size, asize, 1);
                return ptr;
            }
        }
    }

    // 3. 확장불가능
    /**
     * -> [alloc free]이지만, alloc + free가 asize보다 작은 경우 -> [*alloc free alloc] -> [alloc alloc]으로, 확장 불가능한 경우
     **/
    // 위 경로가 모두 실패하면 새 블록을 받아서 옮기는 단순 fallback으로 처리한다.
    // mm_malloc이 내부에서 find_fit/place를 모두 처리하므로 realloc에서는 free block을 직접 다루지 않는 편이 안전하다.
    newptr = mm_malloc(size);
    if (newptr == NULL)
    {
        return NULL;
    }

    // 복사 길이는 "기존 payload 크기"와 "새 요청 크기" 중 작은 값이어야 한다.
    // block 전체 크기를 memcpy에 넣으면 header/footer까지 읽을 수 있다.
    copySize = cur_size - DSIZE;
    if (size < copySize)
    {
        copySize = size;
    }

    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}
