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
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static char *heap_listp;
// 가용 리스트에 제일 앞에 담기는 것
static char *heap_free_listp;

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
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                            // 4 byte 패딩 설정
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // epilogue header

    heap_listp += (2 * WSIZE); // Payload 위치 설정 (prologue payload)
    heap_free_listp = NULL;

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
    /*
     * HINT (Explicit free list, LIFO/stack insert):
     * - free list의 "head"를 `heap_free_listp`로 둔다.
     * - 새로 free된 블록 `p`를 항상 head에 꽂으면 LIFO(스택) 방식이 된다.
     *
     * 연결 규칙(개념):
     * - p->pred = NULL
     * - p->succ = old_head
     * - if (old_head != NULL) old_head->pred = p
     * - heap_free_listp = p
     *
     * 구현 시에는 위의 `pred/succ`를 여기서 만든 매크로를 써서 접근:
     * - `PUT(PRED(p), ...)`, `PUT(SUCC(p), ...)` 또는 `GET_PRED/GET_SUCC` 조합
     * - 주의: `PRED(bp)`/`SUCC(bp)`는 "저장 위치"이고, `GET_PRED/GET_SUCC`는 "저장된 포인터 값"이다.
     */

    char *old_ptr;

    if (p == NULL)
    {
        return;
    }

    old_ptr = heap_free_listp;

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
    heap_free_listp = p;
}

// heap_free_listp에 스택 방식으로 빼기
static void rem_free(void *p)
{
    /*
     * HINT (remove a free block from explicit list):
     * - 제거 대상 `p`의 pred/succ를 읽어서 양 옆을 서로 연결해준다.
     *
     * 케이스 분기(개념):
     * - if (pred == NULL)  => p가 head
     *      heap_free_listp = succ
     *      if (succ != NULL) succ->pred = NULL
     * - else               => p가 중간/끝
     *      pred->succ = succ
     *      if (succ != NULL) succ->pred = pred
     *
     * 선택 힌트:
     * - 디버깅 쉽게 하려면 제거 후 `p->pred`, `p->succ`를 NULL로 지워도 된다(필수 아님).
     * - 인자로 void*만 받으면 제거할 블록을 못 고르니, 보통 시그니처는 `rem_free(char *p)`처럼 "대상 포인터"가 필요하다.
     */
    char *pred;
    char *succ;

    // 중요!!
    if (p == NULL)
    {
        return;
    }

    /**
     * GET2(PRED(p));
     * GET2(SUCC(p));
     */
    pred = GET_PRED(p);
    succ = GET_SUCC(p);

    /**
     * 만약 없어진게 첫번째꺼라면
     * free list 첫번째에 값 집어넣기
     **/
    if (pred == NULL)
    {
        heap_free_listp = succ;
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

/**
 * find_fit
 */
static char *find_fit(size_t size)
{
    // 힙 주소를 first_fit으로 찾는 방식 고안
    // 만약 next_fit이라면, 이미 전역 포인터 변수가 하나 더 있어야 할 거 같다.(내 생각)
    char *bp = heap_free_listp;

    while (bp != NULL)
    {
        // char *pred_header = HDRP(PREV_BLKP(bp)); // 전임자의 HEADER
        // char *pred_footer = FTRP(PREV_BLKP(bp)); // 전임자의 FOOTER
        // char *succ_header = HDRP(NEXT_BLKP(bp)); // 후임자의 HEADER
        // char *succ_footer = FTRP(NEXT_BLKP(bp)); // 후임자의 FOOTER

        // size_t is_pred_alloc = GET_ALLOC(pred_footer);         // 이전이 alloc인지?
        // size_t is_succ_alloc = GET_ALLOC(succ_header);         // 이후가 alloc인지?

        /**
         * 1. size <= free_size 일 경우
         * 2. alloc이 아닌 경우
         **/
        size_t free_size = GET_SIZE(HDRP(bp));
        if (!GET_ALLOC(HDRP(bp)) && free_size >= size)
        {
            return bp;
        }

        bp = GET_SUCC(bp);
    }

    return NULL;
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

// /*
//  * mm_realloc - mm_malloc과 mm_free만으로 단순 구현
//  */
void *mm_realloc(void *ptr, size_t size)
{
    // free alloc free
    // 앞을 봐도 될 것 같은데? -> 그치만 메모리 효율성이 많이 떨어질 것 같다.
    // 1. 사이즈가 감소할 경우 -> place로 자르기
    // 2. 뒤에 free가 있고, 뒤에 사이즈를 더한 값이 size보다 클 경우에는 딱 충분한 크기만큼만 확장 (free일 경우) - place를 사용해서 구현
    // 3. 뒤가 확장이 불가능한 eplilogue header일 경우
    // 4. 이 경우들 다 빼고 예외상황에는 단순구현으로

    void *newptr;
    size_t asize;
    size_t old_block_size;
    size_t next_block_size;
    size_t copySize;
    char *next_bp;

    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    asize = MAX(ALIGN(size + DSIZE), MINBLOCKSIZE);
    old_block_size = GET_SIZE(HDRP(ptr));

    // 1. 요청 크기가 더 작아진 경우에는 같은 자리에서 바로 잘라서 남는 부분만 free block으로 돌려준다.
    if (asize <= old_block_size)
    {
        place_allocated(ptr, old_block_size, asize, 1);
        return ptr;
    }

    next_bp = NEXT_BLKP(ptr);

    // 2. 뒤 블록이 free면 현재 블록과 합쳐서 in-place 확장을 먼저 시도한다.
    if (!GET_ALLOC(HDRP(next_bp)))
    {
        next_block_size = GET_SIZE(HDRP(next_bp));
        if ((old_block_size + next_block_size) >= asize)
        {
            rem_free(next_bp);
            place_allocated(ptr, old_block_size + next_block_size, asize, 1);
            return ptr;
        }
    }

    // 3. 바로 뒤가 epilogue면 heap을 더 늘린 다음 다시 한 번 "뒤 free 확장" 경로를 태운다.
    if (GET_SIZE(HDRP(next_bp)) == 0)
    {
        size_t extend_size = MAX(asize - old_block_size, CHUNKSIZE);
        // 두번째 : 확장할 수 있는 만큼만 확장
        size_t added_size = asize - old_block_size;

        if (extend_heap(added_size / WSIZE) != NULL)
        {
            next_bp = NEXT_BLKP(ptr);
            next_block_size = GET_SIZE(HDRP(next_bp));

            if (!GET_ALLOC(HDRP(next_bp)) && (old_block_size + next_block_size) >= asize)
            {
                rem_free(next_bp);
                place_allocated(ptr, old_block_size + next_block_size, asize, 1);
                return ptr;
            }
        }
    }

    // 4. 위 경로들로 해결되지 않는 경우에만 새 블록을 잡고 복사하는 단순 구현으로 fallback 한다.
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    copySize = old_block_size - DSIZE;
    if (size < copySize)
        copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}
