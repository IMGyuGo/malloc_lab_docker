/*
 * clock.c - x86, Alpha, Sparc 등에서 사이클 카운터 사용 루틴
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include "clock.h"


/*******************************************************
 * 기종 의존 함수
 *
 * 참고: __i386__, __alpha 상수는 GCC가 전처리기에 넣는다.
 * gcc -v로 확인할 수 있다.
 *******************************************************/

#if defined(__i386__)
/*******************************************************
 * Pentium용 start_counter(), get_counter()
 *******************************************************/


/* $begin x86cyclecounter */
/* 사이클 카운터 초기화 */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* *hi, *lo에 카운터의 상·하위 비트를 넣는다.
   rdtsc 어셈블리가 필요하다. */
void access_counter(unsigned *hi, unsigned *lo)
{
    asm("rdtsc; movl %%edx,%0; movl %%eax,%1"   /* 사이클 카운터 읽기 */
	: "=r" (*hi), "=r" (*lo)                /* 결과를 두 출력에 저장 */
	: /* 입력 없음 */
	: "%edx", "%eax");
}

/* 현재 사이클 카운터 값을 기록한다. */
void start_counter()
{
    access_counter(&cyc_hi, &cyc_lo);
}

/* 직전 start_counter 이후 경과 사이클 수를 반환한다. */
double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;

    /* 사이클 카운터 읽기 */
    access_counter(&ncyc_hi, &ncyc_lo);

    /* 배정밀도 뺄셈 */
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
	fprintf(stderr, "오류: 카운터가 음수 값 반환: %.0f\n", result);
    }
    return result;
}
/* $end x86cyclecounter */

#elif defined(__alpha)

/****************************************************
 * Alpha용 start_counter(), get_counter()
 ***************************************************/

/* 사이클 카운터 초기화 */
static unsigned cyc_hi = 0;
static unsigned cyc_lo = 0;


/* Alpha 사이클 타이머로 사이클을 구하고, 측정한 클럭 속도로 초 단위 환산
*/

/*
 * counterRoutine은 Alpha 프로세서 사이클 카운터에 접근하는 명령 배열이다.
 * rpcc로 카운터를 읽는다. 이 64비트 레지스터는 하위 32비트가 현재 프로세스
 * 사이클, 상위 32비트가 월 클록 사이다. 카운터를 읽어 하위 32비트를
 * unsigned int로 바꾼 값이 사용자 공간 카운터다.
 * 참고: 카운터 범위가 매우 제한적이다. 450MHz면 약 9초까지 측정 가능. */
static unsigned int counterRoutine[] =
{
    0x601fc000u,
    0x401f0000u,
    0x6bfa8001u
};

/* 위 명령을 함수로 캐스팅 */
static unsigned int (*counter)(void)= (void *)counterRoutine;


void start_counter()
{
    /* 사이클 카운터 읽기 */
    cyc_hi = 0;
    cyc_lo = counter();
}

double get_counter()
{
    unsigned ncyc_hi, ncyc_lo;
    unsigned hi, lo, borrow;
    double result;
    ncyc_lo = counter();
    ncyc_hi = 0;
    lo = ncyc_lo - cyc_lo;
    borrow = lo > ncyc_lo;
    hi = ncyc_hi - cyc_hi - borrow;
    result = (double) hi * (1 << 30) * 4 + lo;
    if (result < 0) {
	fprintf(stderr, "오류: 사이클 카운터가 음수 반환: %.0f\n", result);
    }
    return result;
}

#else

/****************************************************************
 * 사이클 카운터를 아직 구현하지 않은 기종.
 * 최신 Sparc(v8plus)는 사용자 프로그램에서 사이클 카운터에 접근할 수 있으나
 * 지원하지 않는 Sparc이 많아 여기서는 Sparc 버전을 제공하지 않는다.
 ***************************************************************/

void start_counter()
{
    printf("오류: clock.c의 start_counter가 이 플랫폼에 구현되어 있지 않습니다.\n");
    printf("다른 타이밍 패키지를 config.h에서 선택하세요.\n");
    exit(1);
}

double get_counter()
{
    printf("오류: clock.c의 get_counter가 이 플랫폼에 구현되어 있지 않습니다.\n");
    printf("다른 타이밍 패키지를 config.h에서 선택하세요.\n");
    exit(1);
}
#endif




/*******************************
 * 기종 독립 함수
 ******************************/
double ovhd()
{
    /* 캐시 효과 제거를 위해 두 번 수행 */
    int i;
    double result;

    for (i = 0; i < 2; i++) {
	start_counter();
	result = get_counter();
    }
    return result;
}

/* $begin mhz */
/* sleeptime초 동안 sleep하며 경과 사이클로 클럭 속도 추정 */
double mhz_full(int verbose, int sleeptime)
{
    double rate;

    start_counter();
    sleep(sleeptime);
    rate = get_counter() / (1e6*sleeptime);
    if (verbose)
	printf("프로세서 클럭 속도 ~= %.1f MHz\n", rate);
    return rate;
}
/* $end mhz */

/* 기본 sleeptime 사용 버전 */
double mhz(int verbose)
{
    return mhz_full(verbose, 2);
}

/** 타이머 인터럽트 오버헤드를 보정하는 특수 카운터 */

static double cyc_per_tick = 0.0;

#define NEVENT 100
#define THRESHOLD 1000
#define RECORDTHRESH 3000

/* 타이머 인터럽트에 쓰이는 시간 측정 시도 */
static void callibrate(int verbose)
{
    double oldt;
    struct tms t;
    clock_t oldc;
    int e = 0;

    times(&t);
    oldc = t.tms_utime;
    start_counter();
    oldt = get_counter();
    while (e <NEVENT) {
	double newt = get_counter();

	if (newt-oldt >= THRESHOLD) {
	    clock_t newc;
	    times(&t);
	    newc = t.tms_utime;
	    if (newc > oldc) {
		double cpt = (newt-oldt)/(newc-oldc);
		if ((cyc_per_tick == 0.0 || cyc_per_tick > cpt) && cpt > RECORDTHRESH)
		    cyc_per_tick = cpt;
		/*
		  if (verbose)
		  printf("이벤트 지속 %.0f 사이클, 틱 %d개. 비율 = %f\n",
		  newt-oldt, (int) (newc-oldc), cpt);
		*/
		e++;
		oldc = newc;
	    }
	    oldt = newt;
	}
    }
    if (verbose)
	printf("cyc_per_tick을 %f로 설정\n", cyc_per_tick);
}

static clock_t start_tick = 0;

void start_comp_counter()
{
    struct tms t;

    if (cyc_per_tick == 0.0)
	callibrate(0);
    times(&t);
    start_tick = t.tms_utime;
    start_counter();
}

double get_comp_counter()
{
    double time = get_counter();
    double ctime;
    struct tms t;
    clock_t ticks;

    times(&t);
    ticks = t.tms_utime - start_tick;
    ctime = time - ticks*cyc_per_tick;
    /*
      printf("측정 %.0f 사이클. 틱 = %d. 보정 후 %.0f 사이클\n",
      time, (int) ticks, ctime);
    */
    return ctime;
}

