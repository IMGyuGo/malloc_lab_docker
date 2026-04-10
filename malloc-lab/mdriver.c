/*
 * mdriver.c - CS:APP Malloc Lab 드라이버
 *
 * trace 파일 묶음으로 mm.c의 malloc/free/realloc 구현을 검사한다.
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <float.h>
#include <time.h>

extern char *optarg; /* optarg 선언 (일부 환경용) */

#include "mm.h"
#include "memlib.h"
#include "fsecs.h"
#include "config.h"

/**********************
 * 상수와 매크로
 **********************/

/* 기타 */
#define MAXLINE 1024	   /* 최대 문자열 길이 */
#define HDRLINES 4		   /* trace 파일 헤더 줄 수 */
#define LINENUM(i) (i + 5) /* trace 요청 번호 -> 소스 줄 번호(1부터) */

/* p가 ALIGNMENT바이트 정렬이면 참 */
#define IS_ALIGNED(p) ((((unsigned int)(p)) % ALIGNMENT) == 0)

/******************************
 * 핵심 복합 자료형
 *****************************/

/* 각 블록 페이로드의 구간을 기록 */
typedef struct range_t
{
	char *lo;			  /* 페이로드 하한 주소 */
	char *hi;			  /* 페이로드 상한 주소 */
	struct range_t *next; /* 다음 리스트 원소 */
} range_t;

/* trace 한 연산(할당자 요청)을 나타냄 */
typedef struct
{
	enum
	{
		ALLOC,
		FREE,
		REALLOC
	} type;	   /* 요청 종류 */
	int index; /* 나중에 free()에 쓸 인덱스 */
	int size;  /* alloc/realloc 요청 크기(바이트) */
} traceop_t;

/* trace 파일 하나에 대한 정보 */
typedef struct
{
	int sugg_heapsize;	 /* 권장 힙 크기(미사용) */
	int num_ids;		 /* alloc/realloc id 개수 */
	int num_ops;		 /* 서로 다른 요청 개수 */
	int weight;			 /* 이 trace 가중치(미사용) */
	traceop_t *ops;		 /* 요청 배열 */
	char **blocks;		 /* malloc/realloc이 돌려준 포인터 배열 */
	size_t *block_sizes; /* 대응하는 페이로드 크기 배열 */
} trace_t;

/*
 * fcyc가 시간을 재는 xxx_speed 함수들에 넘길 인자.
 * fcyc는 입력으로 포인터 배열만 받으므로 이 struct가 필요하다.
 */
typedef struct
{
	trace_t *trace;
	range_t *ranges;
} speed_t;

/* 어떤 malloc 구현이 어떤 trace에서 낸 주요 통계 요약 */
typedef struct
{
	/* libc malloc과 학생 mm.c 모두에 대해 정의 */
	double ops;	 /* trace 안 연산 수(malloc/free/realloc) */
	int valid;	 /* 할당자가 trace를 올바르게 처리했는지 */
	double secs; /* trace 실행에 걸린 시간(초) */

	/* 학생 malloc 패키지에만 정의 */
	double util; /* 이 trace의 공간 이용률(libc는 항상 0) */

	/* 참고: valid가 참일 때만 secs, util이 의미 있음 */
} stats_t;

/********************
 * 전역 변수
 *******************/
int verbose = 0;	   /* 상세 출력 플래그 */
static int errors = 0; /* 학생 malloc 실행 중 발견한 오류 수 */
char msg[MAXLINE];	   /* 오류 메시지 조합용 */

/* 기본 trace 파일이 있는 디렉터리 */
static char tracedir[MAXLINE] = TRACEDIR;

/* 기본 trace 파일 이름들 */
static char *default_tracefiles[] = {
	DEFAULT_TRACEFILES, NULL};

/*********************
 * 함수 원형
 *********************/

/* range 리스트 조작 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum);
static void remove_range(range_t **ranges, char *lo);
static void clear_ranges(range_t **ranges);

/* trace 읽기·할당·해제 */
static trace_t *read_trace(char *tracedir, char *filename);
static void free_trace(trace_t *trace);

/* libc malloc의 정확성·속도 평가 */
static int eval_libc_valid(trace_t *trace, int tracenum);
static void eval_libc_speed(void *ptr);

/* mm.c 학생 malloc의 정확성·공간 이용·속도 평가 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges);
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges);
static void eval_mm_speed(void *ptr);

/* 기타 보조 루틴 */
static void printresults(int n, stats_t *stats);
static void usage(void);
static void unix_error(char *msg);
static void malloc_error(int tracenum, int opnum, char *msg);
static void app_error(char *msg);

/**************
 * main
 **************/
int main(int argc, char **argv)
{
	int i;
	int c;
	char **tracefiles = NULL;	/* trace 파일 이름 배열(NULL 종료) */
	int num_tracefiles = 0;		/* 그 배열의 trace 개수 */
	trace_t *trace = NULL;		/* 메모리에 올린 trace 하나 */
	range_t *ranges = NULL;		/* 한 trace의 블록 구간 추적 */
	stats_t *libc_stats = NULL; /* trace별 libc 통계 */
	stats_t *mm_stats = NULL;	/* trace별 mm(학생) 통계 */
	speed_t speed_params;		/* xx_speed 루틴에 넘길 인자 */

	int team_check = 1; /* 설정 시 팀 정보 검사(-a로 해제) */
	int run_libc = 0;	/* 설정 시 libc malloc 실행(-l) */
	int autograder = 0; /* 설정 시 autograder용 요약 출력(-g) */

	/* 성능 지수 계산용 임시 */
	double secs, ops, util, avg_mm_util, avg_mm_throughput, p1, p2, perfindex;
	int numcorrect;

	/*
	 * 명령줄 인자 읽기·해석
	 */
	while ((c = getopt(argc, argv, "f:t:hvVgal")) != EOF)
	{
		printf("getopt returned: %d\n", c); /* 디버깅용 출력 */

		switch (c)
		{
		case 'g': /* autograder용 요약 정보 */
			autograder = 1;
			break;
		case 'f': /* trace 파일 하나만 사용(현재 디렉터리 기준 상대 경로) */
			num_tracefiles = 1;
			if ((tracefiles = realloc(tracefiles, 2 * sizeof(char *))) == NULL)
				unix_error("ERROR: realloc failed in main");
			strcpy(tracedir, "./");
			tracefiles[0] = strdup(optarg);
			tracefiles[1] = NULL;
			break;
		case 't':					 /* trace가 있는 디렉터리 */
			if (num_tracefiles == 1) /* 이미 -f가 있으면 무시 */
				break;
			strcpy(tracedir, optarg);
			if (tracedir[strlen(tracedir) - 1] != '/')
				strcat(tracedir, "/"); /* 경로는 항상 "/"로 끝남 */
			break;
		case 'a': /* 팀 구조 검사 안 함 */
			team_check = 0;
			break;
		case 'l': /* libc malloc 실행 */
			run_libc = 1;
			break;
		case 'v': /* trace별 성능 요약 출력 */
			verbose = 1;
			break;
		case 'V': /* -v보다 더 상세 */
			verbose = 2;
			break;
		case 'h': /* 도움말 출력 */
			usage();
			exit(0);
		default:
			usage();
			exit(1);
		}
	}

	/*
	 * 팀 정보 확인·출력
	 */
	if (team_check)
	{
		/* 학생은 mm.c에 팀 정보를 채워야 함 */
		if (!strcmp(team.teamname, ""))
		{
			printf("ERROR: Please provide the information about your team in mm.c.\n");
			exit(1);
		}
		else
			printf("Team Name:%s\n", team.teamname);
		if ((*team.name1 == '\0') || (*team.id1 == '\0'))
		{
			printf("ERROR.  You must fill in all team member 1 fields!\n");
			exit(1);
		}
		else
			printf("Member 1 :%s:%s\n", team.name1, team.id1);

		if (((*team.name2 != '\0') && (*team.id2 == '\0')) ||
			((*team.name2 == '\0') && (*team.id2 != '\0')))
		{
			printf("ERROR.  You must fill in all or none of the team member 2 ID fields!\n");
			exit(1);
		}
		else if (*team.name2 != '\0')
			printf("Member 2 :%s:%s\n", team.name2, team.id2);
	}

	/*
	 * -f가 없으면 default_traces[]에 정의된 전체 trace 파일 사용
	 */
	if (tracefiles == NULL)
	{
		tracefiles = default_tracefiles;
		num_tracefiles = sizeof(default_tracefiles) / sizeof(char *) - 1;
		printf("Using default tracefiles in %s\n", tracedir);
	}

	/* 타이밍 패키지 초기화 */
	init_fsecs();

	/*
	 * 선택적으로 libc malloc 실행·평가
	 */
	if (run_libc)
	{
		if (verbose > 1)
			printf("\nTesting libc malloc\n");

		/* trace 파일마다 stats_t 하나인 libc 통계 배열 할당 */
		libc_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
		if (libc_stats == NULL)
			unix_error("libc_stats calloc in main failed");

		/* K-best 방식으로 libc malloc 평가 */
		for (i = 0; i < num_tracefiles; i++)
		{
			trace = read_trace(tracedir, tracefiles[i]);
			libc_stats[i].ops = trace->num_ops;
			if (verbose > 1)
				printf("Checking libc malloc for correctness, ");
			libc_stats[i].valid = eval_libc_valid(trace, i);
			if (libc_stats[i].valid)
			{
				speed_params.trace = trace;
				if (verbose > 1)
					printf("and performance.\n");
				libc_stats[i].secs = fsecs(eval_libc_speed, &speed_params);
			}
			free_trace(trace);
		}

		/* libc 결과를 간단한 표로 출력 */
		if (verbose)
		{
			printf("\nResults for libc malloc:\n");
			printresults(num_tracefiles, libc_stats);
		}
	}

	/*
	 * 학생 mm 패키지는 항상 실행·평가
	 */
	if (verbose > 1)
		printf("\nTesting mm malloc\n");

	/* trace 파일마다 stats_t 하나인 mm 통계 배열 할당 */
	mm_stats = (stats_t *)calloc(num_tracefiles, sizeof(stats_t));
	if (mm_stats == NULL)
		unix_error("mm_stats calloc in main failed");

	/* memlib.c의 시뮬 메모리 초기화 */
	mem_init();

	/* K-best 방식으로 학생 mm malloc 평가 */
	for (i = 0; i < num_tracefiles; i++)
	{
		trace = read_trace(tracedir, tracefiles[i]);
		mm_stats[i].ops = trace->num_ops;
		if (verbose > 1)
			printf("Checking mm_malloc for correctness, ");
		mm_stats[i].valid = eval_mm_valid(trace, i, &ranges);
		if (mm_stats[i].valid)
		{
			if (verbose > 1)
				printf("efficiency, ");
			mm_stats[i].util = eval_mm_util(trace, i, &ranges);
			speed_params.trace = trace;
			speed_params.ranges = ranges;
			if (verbose > 1)
				printf("and performance.\n");
			mm_stats[i].secs = fsecs(eval_mm_speed, &speed_params);
		}
		free_trace(trace);
	}

	/* mm 결과를 간단한 표로 출력 */
	if (verbose)
	{
		printf("\nResults for mm malloc:\n");
		printresults(num_tracefiles, mm_stats);
		printf("\n");
	}

	/*
	 * 학생 mm 패키지 전체 통계 누적
	 */
	secs = 0;
	ops = 0;
	util = 0;
	numcorrect = 0;
	for (i = 0; i < num_tracefiles; i++)
	{
		secs += mm_stats[i].secs;
		ops += mm_stats[i].ops;
		util += mm_stats[i].util;
		if (mm_stats[i].valid)
			numcorrect++;
	}
	avg_mm_util = util / num_tracefiles;

	/*
	 * 성능 지수 계산·출력
	 */
	if (errors == 0)
	{
		avg_mm_throughput = ops / secs;

		p1 = UTIL_WEIGHT * avg_mm_util;
		if (avg_mm_throughput > AVG_LIBC_THRUPUT)
		{
			p2 = (double)(1.0 - UTIL_WEIGHT);
		}
		else
		{
			p2 = ((double)(1.0 - UTIL_WEIGHT)) *
				 (avg_mm_throughput / AVG_LIBC_THRUPUT);
		}

		perfindex = (p1 + p2) * 100.0;
		printf("Perf index = %.0f (util) + %.0f (thru) = %.0f/100\n",
			   p1 * 100,
			   p2 * 100,
			   perfindex);
	}
	else
	{ /* 오류가 있었음 */
		perfindex = 0.0;
		printf("Terminated with %d errors\n", errors);
	}

	if (autograder)
	{
		printf("correct:%d\n", numcorrect);
		printf("perfidx:%.0f\n", perfindex);
	}

	exit(0);
}

/*****************************************************************
 * range 리스트: 할당된 각 블록 페이로드의 구간을 추적한다.
 * 겹치는 할당 블록이 있는지 검사하는 데 쓴다.
 ****************************************************************/

/*
 * add_range - trace tracenum의 요청 opnum에 따라 학생 mm_malloc으로
 *     주소 lo에 size바이트 블록을 막 할당했다. 블록이 올바른지 검사한 뒤
 *     range 구조체를 만들어 리스트에 넣는다.
 */
static int add_range(range_t **ranges, char *lo, int size,
					 int tracenum, int opnum)
{
	char *hi = lo + size - 1;
	range_t *p;
	char msg[MAXLINE];

	assert(size > 0);

	/* 페이로드 주소는 ALIGNMENT바이트 정렬이어야 함 */
	if (!IS_ALIGNED(lo))
	{
		sprintf(msg, "Payload address (%p) not aligned to %d bytes",
				lo, ALIGNMENT);
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* 페이로드는 힙 범위 안에 있어야 함 */
	if ((lo < (char *)mem_heap_lo()) || (lo > (char *)mem_heap_hi()) ||
		(hi < (char *)mem_heap_lo()) || (hi > (char *)mem_heap_hi()))
	{
		sprintf(msg, "Payload (%p:%p) lies outside heap (%p:%p)",
				lo, hi, mem_heap_lo(), mem_heap_hi());
		malloc_error(tracenum, opnum, msg);
		return 0;
	}

	/* 다른 페이로드와 겹치면 안 됨 */
	for (p = *ranges; p != NULL; p = p->next)
	{
		if ((lo >= p->lo && lo <= p->hi) ||
			(hi >= p->lo && hi <= p->hi))
		{
			sprintf(msg, "Payload (%p:%p) overlaps another payload (%p:%p)\n",
					lo, hi, p->lo, p->hi);
			malloc_error(tracenum, opnum, msg);
			return 0;
		}
	}

	/*
	 * 문제 없으면 range 구조체를 만들어 이 블록 구간을 리스트에 넣는다.
	 */
	if ((p = (range_t *)malloc(sizeof(range_t))) == NULL)
		unix_error("malloc error in add_range");
	p->next = *ranges;
	p->lo = lo;
	p->hi = hi;
	*ranges = p;
	return 1;
}

/*
 * remove_range - 페이로드 시작 주소가 lo인 블록의 range 레코드 제거
 */
static void remove_range(range_t **ranges, char *lo)
{
	range_t *p;
	range_t **prevpp = ranges;
	int size;

	for (p = *ranges; p != NULL; p = p->next)
	{
		if (p->lo == lo)
		{
			*prevpp = p->next;
			size = p->hi - p->lo + 1;
			free(p);
			break;
		}
		prevpp = &(p->next);
	}
}

/*
 * clear_ranges - 한 trace에 대한 range 레코드를 모두 해제
 */
static void clear_ranges(range_t **ranges)
{
	range_t *p;
	range_t *pnext;

	for (p = *ranges; p != NULL; p = pnext)
	{
		pnext = p->next;
		free(p);
	}
	*ranges = NULL;
}

/**********************************************
 * trace 파일 조작 루틴
 *********************************************/

/*
 * read_trace - trace 파일을 읽어 메모리에 적재
 */
static trace_t *read_trace(char *tracedir, char *filename)
{
	FILE *tracefile;
	trace_t *trace;
	char type[MAXLINE];
	char path[MAXLINE];
	unsigned index, size;
	unsigned max_index = 0;
	unsigned op_index;

	if (verbose > 1)
		printf("Reading tracefile: %s\n", filename);

	/* trace 레코드 할당 */
	if ((trace = (trace_t *)malloc(sizeof(trace_t))) == NULL)
		unix_error("malloc 1 failed in read_trance");

	/* trace 파일 헤더 읽기 */
	strcpy(path, tracedir);
	strcat(path, filename);
	if ((tracefile = fopen(path, "r")) == NULL)
	{
		sprintf(msg, "Could not open %s in read_trace", path);
		unix_error(msg);
	}
	fscanf(tracefile, "%d", &(trace->sugg_heapsize)); /* 미사용 */
	fscanf(tracefile, "%d", &(trace->num_ids));
	fscanf(tracefile, "%d", &(trace->num_ops));
	fscanf(tracefile, "%d", &(trace->weight)); /* 미사용 */

	/* trace의 각 요청 줄을 이 배열에 저장 */
	if ((trace->ops =
			 (traceop_t *)malloc(trace->num_ops * sizeof(traceop_t))) == NULL)
		unix_error("malloc 2 failed in read_trace");

	/* 할당된 블록 포인터 배열 */
	if ((trace->blocks =
			 (char **)malloc(trace->num_ids * sizeof(char *))) == NULL)
		unix_error("malloc 3 failed in read_trace");

	/* 각 블록의 바이트 크기 배열 */
	if ((trace->block_sizes =
			 (size_t *)malloc(trace->num_ids * sizeof(size_t))) == NULL)
		unix_error("malloc 4 failed in read_trace");

	/* trace 파일의 모든 요청 줄 읽기 */
	index = 0;
	op_index = 0;
	while (fscanf(tracefile, "%s", type) != EOF)
	{
		switch (type[0])
		{
		case 'a':
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = ALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'r':
			fscanf(tracefile, "%u %u", &index, &size);
			trace->ops[op_index].type = REALLOC;
			trace->ops[op_index].index = index;
			trace->ops[op_index].size = size;
			max_index = (index > max_index) ? index : max_index;
			break;
		case 'f':
			fscanf(tracefile, "%ud", &index);
			trace->ops[op_index].type = FREE;
			trace->ops[op_index].index = index;
			break;
		default:
			printf("Bogus type character (%c) in tracefile %s\n",
				   type[0], path);
			exit(1);
		}
		op_index++;
	}
	fclose(tracefile);
	assert(max_index == trace->num_ids - 1);
	assert(trace->num_ops == op_index);

	return trace;
}

/*
 * free_trace - read_trace()에서 할당한 trace 레코드와 세 배열 해제
 */
void free_trace(trace_t *trace)
{
	free(trace->ops); /* 세 배열 해제 */
	free(trace->blocks);
	free(trace->block_sizes);
	free(trace); /* trace 레코드 자체도 해제 */
}

/**********************************************************************
 * libc 및 mm malloc 패키지의 정확성·공간 이용·처리량을 평가하는 함수들
 **********************************************************************/

/*
 * eval_mm_valid - mm malloc 패키지 정확성 검사
 */
static int eval_mm_valid(trace_t *trace, int tracenum, range_t **ranges)
{
	int i, j;
	int index;
	int size;
	int oldsize;
	char *newp;
	char *oldp;
	char *p;

	/* 힙 리셋 및 range 리스트의 레코드 모두 해제 */
	mem_reset_brk();
	clear_ranges(ranges);

	/* mm 패키지 초기화 함수 호출 */
	if (mm_init() < 0)
	{
		malloc_error(tracenum, 0, "mm_init failed.");
		return 0;
	}

	/* trace의 연산을 순서대로 해석 */
	for (i = 0; i < trace->num_ops; i++)
	{
		index = trace->ops[i].index;
		size = trace->ops[i].size;

		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */

			/* 학생 malloc 호출 */
			if ((p = mm_malloc(size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_malloc failed.");
				return 0;
			}

			/*
			 * 새 블록 구간이 올바른지 검사하고, 통과하면 range 리스트에 추가.
			 * 정렬이 맞아야 하고 현재 할당된 블록과 겹치면 안 된다.
			 */
			if (add_range(ranges, p, size, tracenum, i) == 0)
				return 0;

			/* 추가(cgw): 구간을 index의 하위 바이트로 채움. 나중에 블록을
			 * realloc할 때 옛 데이터가 새 블록으로 복사됐는지 확인하는 데 쓴다.
			 */
			memset(p, index & 0xFF, size);

			/* 영역 기억 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;
			break;

		case REALLOC: /* mm_realloc */

			/* 학생 realloc 호출 */
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, size)) == NULL)
			{
				malloc_error(tracenum, i, "mm_realloc failed.");
				return 0;
			}

			/* range 리스트에서 옛 영역 제거 */
			remove_range(ranges, oldp);

			/* 새 블록 검사 후 range 리스트에 추가 */
			if (add_range(ranges, newp, size, tracenum, i) == 0)
				return 0;

			/* 추가(cgw): 새 블록에 옛 블록 데이터가 있는지 확인한 뒤,
			 * 새 블록을 새 index의 하위 바이트로 채운다.
			 */
			oldsize = trace->block_sizes[index];
			if (size < oldsize)
				oldsize = size;
			for (j = 0; j < oldsize; j++)
			{
				if (newp[j] != (index & 0xFF))
				{
					malloc_error(tracenum, i, "mm_realloc did not preserve the "
											  "data from old block");
					return 0;
				}
			}
			memset(newp, index & 0xFF, size);

			/* 영역 기억 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = size;
			break;

		case FREE: /* mm_free */

			/* 리스트에서 영역 제거 후 학생 free 호출 */
			p = trace->blocks[index];
			remove_range(ranges, p);
			mm_free(p);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
	}

	/* 현재까지는 유효한 malloc 패키지로 본다 */
	return 1;
}

/*
 * eval_mm_util - 학생 패키지의 공간 이용률 평가
 *   이상적인 할당자(틈·내부 단편화 없음)가 쓸 힙의 최고점 "hwm"을 기준으로 한다.
 *   이용률은 hwm/heapsize이고, heapsize는 trace를 학생 malloc으로 돌린 뒤
 *   힙 크기(바이트)다. mem_sbrk() 구현은 brk를 줄이지 못하게 하므로
 *   brk가 항상 힙의 최고점이다.
 *
 */
static double eval_mm_util(trace_t *trace, int tracenum, range_t **ranges)
{
	int i;
	int index;
	int size, newsize, oldsize;
	int max_total_size = 0;
	int total_size = 0;
	char *p;
	char *newp, *oldp;

	/* 힙과 mm malloc 패키지 초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_util");

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_alloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;

			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc failed in eval_mm_util");

			/* 영역과 크기 기억 */
			trace->blocks[index] = p;
			trace->block_sizes[index] = size;

			/* 현재 할당된 블록들의 총 크기 추적 */
			total_size += size;

			/* 통계 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldsize = trace->block_sizes[index];

			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc failed in eval_mm_util");

			/* 영역과 크기 기억 */
			trace->blocks[index] = newp;
			trace->block_sizes[index] = newsize;

			/* 현재 할당된 블록들의 총 크기 추적 */
			total_size += (newsize - oldsize);

			/* 통계 갱신 */
			max_total_size = (total_size > max_total_size) ? total_size : max_total_size;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			size = trace->block_sizes[index];
			p = trace->blocks[index];

			mm_free(p);

			/* 현재 할당된 블록들의 총 크기 추적 */
			total_size -= size;

			break;

		default:
			app_error("Nonexistent request type in eval_mm_util");
		}
	}

	return ((double)max_total_size / (double)mem_heapsize());
}

/*
 * eval_mm_speed - fcyc()가 mm malloc 패키지 실행 시간을 재기 위해 호출하는 함수
 */
static void eval_mm_speed(void *ptr)
{
	int i, index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	/* 힙 리셋 및 mm 패키지 초기화 */
	mem_reset_brk();
	if (mm_init() < 0)
		app_error("mm_init failed in eval_mm_speed");

	/* trace 요청을 하나씩 처리 */
	for (i = 0; i < trace->num_ops; i++)
		switch (trace->ops[i].type)
		{

		case ALLOC: /* mm_malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = mm_malloc(size)) == NULL)
				app_error("mm_malloc error in eval_mm_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* mm_realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = mm_realloc(oldp, newsize)) == NULL)
				app_error("mm_realloc error in eval_mm_speed");
			trace->blocks[index] = newp;
			break;

		case FREE: /* mm_free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			mm_free(block);
			break;

		default:
			app_error("Nonexistent request type in eval_mm_valid");
		}
}

/*
 * eval_libc_valid - libc malloc이 trace 집합에서 끝까지 돌아가는지 확인한다.
 *    malloc 호출이 하나라도 실패하면 보수적으로 종료한다.
 *
 */
static int eval_libc_valid(trace_t *trace, int tracenum)
{
	int i, newsize;
	char *p, *newp, *oldp;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{

		case ALLOC: /* malloc */
			if ((p = malloc(trace->ops[i].size)) == NULL)
			{
				malloc_error(tracenum, i, "libc malloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = p;
			break;

		case REALLOC: /* realloc */
			newsize = trace->ops[i].size;
			oldp = trace->blocks[trace->ops[i].index];
			if ((newp = realloc(oldp, newsize)) == NULL)
			{
				malloc_error(tracenum, i, "libc realloc failed");
				unix_error("System message");
			}
			trace->blocks[trace->ops[i].index] = newp;
			break;

		case FREE: /* free */
			free(trace->blocks[trace->ops[i].index]);
			break;

		default:
			app_error("invalid operation type  in eval_libc_valid");
		}
	}

	return 1;
}

/*
 * eval_libc_speed - fcyc()가 trace 집합에서 libc malloc 실행 시간을 재기 위해 쓰는 함수
 */
static void eval_libc_speed(void *ptr)
{
	int i;
	int index, size, newsize;
	char *p, *newp, *oldp, *block;
	trace_t *trace = ((speed_t *)ptr)->trace;

	for (i = 0; i < trace->num_ops; i++)
	{
		switch (trace->ops[i].type)
		{
		case ALLOC: /* malloc */
			index = trace->ops[i].index;
			size = trace->ops[i].size;
			if ((p = malloc(size)) == NULL)
				unix_error("malloc failed in eval_libc_speed");
			trace->blocks[index] = p;
			break;

		case REALLOC: /* realloc */
			index = trace->ops[i].index;
			newsize = trace->ops[i].size;
			oldp = trace->blocks[index];
			if ((newp = realloc(oldp, newsize)) == NULL)
				unix_error("realloc failed in eval_libc_speed\n");

			trace->blocks[index] = newp;
			break;

		case FREE: /* free */
			index = trace->ops[i].index;
			block = trace->blocks[index];
			free(block);
			break;
		}
	}
}

/*************************************
 * 기타 보조 루틴
 ************************************/

/*
 * printresults - 어떤 malloc 패키지의 성능 요약 출력
 */
static void printresults(int n, stats_t *stats)
{
	int i;
	double secs = 0;
	double ops = 0;
	double util = 0;

	/* trace별 개별 결과 출력 */
	printf("%5s%7s %5s%8s%10s%6s\n",
		   "trace", " valid", "util", "ops", "secs", "Kops");
	for (i = 0; i < n; i++)
	{
		if (stats[i].valid)
		{
			printf("%2d%10s%5.0f%%%8.0f%10.6f%6.0f\n",
				   i,
				   "yes",
				   stats[i].util * 100.0,
				   stats[i].ops,
				   stats[i].secs,
				   (stats[i].ops / 1e3) / stats[i].secs);
			secs += stats[i].secs;
			ops += stats[i].ops;
			util += stats[i].util;
		}
		else
		{
			printf("%2d%10s%6s%8s%10s%6s\n",
				   i,
				   "no",
				   "-",
				   "-",
				   "-",
				   "-");
		}
	}

	/* 전체 trace에 대한 합계 결과 출력 */
	if (errors == 0)
	{
		printf("%12s%5.0f%%%8.0f%10.6f%6.0f\n",
			   "Total       ",
			   (util / n) * 100.0,
			   ops,
			   secs,
			   (ops / 1e3) / secs);
	}
	else
	{
		printf("%12s%6s%8s%10s%6s\n",
			   "Total       ",
			   "-",
			   "-",
			   "-",
			   "-");
	}
}

/*
 * app_error - 임의의 응용 프로그램 오류 보고
 */
void app_error(char *msg)
{
	printf("%s\n", msg);
	exit(1);
}

/*
 * unix_error - Unix 스타일 오류 보고
 */
void unix_error(char *msg)
{
	printf("%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * malloc_error - mm_malloc 패키지에서 반환된 오류 보고
 */
void malloc_error(int tracenum, int opnum, char *msg)
{
	errors++;
	printf("ERROR [trace %d, line %d]: %s\n", tracenum, LINENUM(opnum), msg);
}

/*
 * usage - 명령줄 인자 설명
 */
static void usage(void)
{
	fprintf(stderr, "Usage: mdriver [-hvVal] [-f <file>] [-t <dir>]\n");
	fprintf(stderr, "Options\n");
	fprintf(stderr, "\t-a         Don't check the team structure.\n");
	fprintf(stderr, "\t-f <file>  Use <file> as the trace file.\n");
	fprintf(stderr, "\t-g         Generate summary info for autograder.\n");
	fprintf(stderr, "\t-h         Print this message.\n");
	fprintf(stderr, "\t-l         Run libc malloc as well.\n");
	fprintf(stderr, "\t-t <dir>   Directory to find default traces.\n");
	fprintf(stderr, "\t-v         Print per-trace performance breakdowns.\n");
	fprintf(stderr, "\t-V         Print additional debug info.\n");
}
