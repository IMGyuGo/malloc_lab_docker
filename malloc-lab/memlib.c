/*
 * memlib.c - 메모리 시스템을 시뮬레이션하는 모듈. 학생 malloc과
 *            libc의 malloc 호출을 섞어 쓸 수 있게 해 준다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>

#include "memlib.h"
#include "config.h"

/* 비공개 변수 */
static char *mem_start_brk;  /* 힙의 첫 바이트를 가리킴 */
static char *mem_brk;        /* 힙의 마지막 바이트를 가리킴 */
static char *mem_max_addr;   /* 허용되는 최대 힙 주소 */

/*
 * mem_init - 메모리 시스템 모델 초기화
 */
void mem_init(void)
{
    /* 가용 VM을 모델링할 저장 공간 할당 */
    if ((mem_start_brk = (char *)malloc(MAX_HEAP)) == NULL) {
	fprintf(stderr, "mem_init_vm: malloc error\n");
	exit(1);
    }

    mem_max_addr = mem_start_brk + MAX_HEAP;  /* 허용 최대 힙 주소 */
    mem_brk = mem_start_brk;                  /* 처음에는 힙이 비어 있음 */
}

/*
 * mem_deinit - 메모리 시스템 모델이 쓰던 저장 공간 해제
 */
void mem_deinit(void)
{
    free(mem_start_brk);
}

/*
 * mem_reset_brk - 시뮬레이션 brk를 비어 있는 힙으로 리셋
 */
void mem_reset_brk()
{
    mem_brk = mem_start_brk;
}

/*
 * mem_sbrk - sbrk 함수의 단순 모델. 힙을 incr 바이트만큼 확장하고
 *    새 영역의 시작 주소를 반환한다. 이 모델에서는 힙을 줄일 수 없다.
 */
void *mem_sbrk(int incr)
{
    char *old_brk = mem_brk;

    if ( (incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
	errno = ENOMEM;
	fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
	return (void *)-1;
    }
    mem_brk += incr;
    return (void *)old_brk;
}

/*
 * mem_heap_lo - 힙 첫 바이트 주소 반환
 */
void *mem_heap_lo()
{
    return (void *)mem_start_brk;
}

/*
 * mem_heap_hi - 힙 마지막 바이트 주소 반환
 */
void *mem_heap_hi()
{
    return (void *)(mem_brk - 1);
}

/*
 * mem_heapsize() - 힙 크기(바이트) 반환
 */
size_t mem_heapsize()
{
    return (size_t)(mem_brk - mem_start_brk);
}

/*
 * mem_pagesize() - 시스템 페이지 크기 반환
 */
size_t mem_pagesize()
{
    return (size_t)getpagesize();
}
