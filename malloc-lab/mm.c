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
#define CHUNKSIZE (1 << 12)
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

/**
 * 사이즈를 지정할 때, 왜 size_t로 8바이트 데이터 타입을 쓰는지 생각해보니
 * 메모리 용량이 4바이트일 경우 2^32 = 2^10 = 1000byte -> (1000)^3 * 2^2 = 4 * 10^9 = 4,000,000kB = 4,000Mb = 4Gbyte
 * 메모리 용량이 4기가 보다 더 클 수도 있기 때문에 (나의 생각)
 */
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
// 여기서 footer를 안 쓰면, footer에 정보를 넣을 수 가 없다.
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static char *heap_listp;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static char *find_fit(size_t size);
static void place(char *p, size_t asize);

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
    if ((heap_listp = mem_sbrk(2 * DSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);                            // 4 byte 패딩 설정
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // Epilogue header
    // PUT(heap_listp, PACK(DSIZE, 1));           // prologue header
    // PUT(heap_listp + (1 * DSIZE), PACK(0, 1)); // Epilogue header

    heap_listp += DSIZE; // Payload 위치 설정

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
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

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc)
    {
        return bp;
    }

    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
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

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + SIZE_T_SIZE);

    // 할당 하기 전에 영역 전부를 다 살펴보고
    // place를 통해 그 자리에 할당
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    // 이걸 왜하지? 무조건 CHUNKSIZE만큼만 확장하면 안되나?
    // 정말 모르겠지만,g.g.
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
    char *bp = heap_listp + DSIZE;

    while (1)
    {
        // char *pred_header = HDRP(PREV_BLKP(bp)); // 전임자의 HEADER
        // char *pred_footer = FTRP(PREV_BLKP(bp)); // 전임자의 FOOTER
        // char *succ_header = HDRP(NEXT_BLKP(bp)); // 후임자의 HEADER
        // char *succ_footer = FTRP(NEXT_BLKP(bp)); // 후임자의 FOOTER

        // size_t is_pred_alloc = GET_ALLOC(pred_footer);         // 이전이 alloc인지?
        size_t is_alloc = GET_ALLOC(HDRP(bp)); // 현재가 alloc인지? -> FTRP(bp) 써도 됨
        // size_t is_succ_alloc = GET_ALLOC(succ_header);         // 이후가 alloc인지?

        /**
         * 1. size <= free_size 일 경우
         * 2. alloc이 아닌 경우
         **/
        size_t free_size = GET_SIZE(HDRP(bp));
        if (free_size == 0)
            return NULL;
        if (is_alloc || free_size < size)
        {
            bp += free_size;
            continue;
        }

        return bp;
    }
}

static void place(char *p, size_t asize)
{
    // 실제 사이즈
    size_t real_size = GET_SIZE(HDRP(p));

    if ((real_size - asize) < 2 * DSIZE)
    {
        PUT(HDRP(p), PACK(real_size, 1));
        PUT(FTRP(p), PACK(real_size, 1));
    }
    else
    {
        PUT(HDRP(p), PACK(asize, 1));
        PUT(FTRP(p), PACK(asize, 1));
        char *split_ptr = NEXT_BLKP(p);
        PUT(HDRP(split_ptr), PACK(real_size - asize, 0));
        PUT(FTRP(split_ptr), PACK(real_size - asize, 0));

        coalesce(split_ptr);
    }
}

/*
 * mm_free - 블록 해제 시 아무 동작도 하지 않음.
 */
void mm_free(void *ptr)
{
    // *(size_t *)ptr = 0;
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

void *mm_realloc(void *ptr, size_t size)
{
    void *newptr;
    void *bp = ptr;

    // if (size == 0)
    // {
    //     mm_free(bp);
    //     return NULL;
    // }

    // if (all_size <= cur_size)
    // {
    //     PUT(HDRP(bp), PACK(all_size, 0));
    //     PUT(FTRP(bp), PACK(all_size, 0));
    //     return bp;
    // }

    if (ptr == NULL)
    {
        return mm_malloc(size);
    }

    size_t all_size = ALIGN(size) + DSIZE;
    size_t cur_size = GET_SIZE(HDRP(bp));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));

    if ((!GET_ALLOC(HDRP(NEXT_BLKP(bp)))) &&
        (all_size <= cur_size + next_size))
    {

        PUT(HDRP(bp), PACK((cur_size + next_size), 0));
        PUT(FTRP(bp), PACK((cur_size + next_size), 0));

        place(bp, all_size);

        return bp;
    }
    else
    {
        newptr = find_fit(all_size);

        if (newptr == NULL)
        {
            newptr = extend_heap(MAX(all_size, CHUNKSIZE) / WSIZE);

            next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));

            if (!GET_ALLOC(HDRP(NEXT_BLKP(bp))) && all_size <= cur_size + next_size)
            {
                PUT(HDRP(bp), PACK((cur_size + next_size), 0));
                PUT(FTRP(bp), PACK((cur_size + next_size), 0));

                place(bp, all_size);

                return bp;
            }

            // alloc free alloc epilogue 블럭일 경우, free 부분을 확장 후, 뒤로 옮기기 위해서 따로 조건을 추가
            place(newptr, all_size);
            memcpy(newptr, bp, all_size - DSIZE);
            mm_free(bp);
            return newptr;
        }

        place(newptr, all_size);
        memcpy(newptr, bp, all_size - DSIZE);
        mm_free(bp);

        return newptr;
    }
}

// void *mm_realloc(void *ptr, size_t size)
// {
//     void *newptr;
//     size_t copySize;
//     size_t oldpayload;
//     if (ptr == NULL)
//         return mm_malloc(size);
//     if (size == 0)
//     {
//         mm_free(ptr);
//         return NULL;
//     }
//     newptr = mm_malloc(size);
//     if (newptr == NULL)
//         return NULL;
//     oldpayload = GET_SIZE(HDRP(ptr)) - DSIZE;
//     copySize = (size < oldpayload) ? size : oldpayload;
//     memcpy(newptr, ptr, copySize);
//     mm_free(ptr);
//     return newptr;
// }
