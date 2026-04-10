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
    "ateam",
    /* 1번 멤버 전체 이름 */
    "Harry Bovik",
    /* 1번 멤버 이메일 */
    "bovik@cs.cmu.edu",
    /* 2번 멤버 전체 이름 (없으면 빈 문자열) */
    "",
    /* 2번 멤버 이메일 (없으면 빈 문자열) */
    ""};

/* 단일 워드(4) 또는 더블 워드(8) 정렬 */
#define ALIGNMENT 8

/* 크기를 ALIGNMENT의 배수로 올림 */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*
 * mm_init - malloc 패키지 초기화.
 */
int mm_init(void)
{
    return 0;
}

/*
 * mm_malloc - brk 포인터를 늘려 블록 할당.
 *     항상 정렬 배수 크기의 블록을 할당한다.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else
    {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - 블록 해제 시 아무 동작도 하지 않음.
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - mm_malloc과 mm_free만으로 단순 구현
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
