/*
 * ftimer.c - 함수 f가 쓰는 시간(초) 추정
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 *
 * 함수 f의 실행 시간(초)을 추정하는 타이머.
 *    ftimer_itimer: 인터벌 타이머 사용
 *    ftimer_gettod: gettimeofday 사용
 */
#include <stdio.h>
#include <sys/time.h>
#include "ftimer.h"

/* 함수 원형 */
static void init_etime(void);
static double get_etime(void);

/*
 * ftimer_itimer - 인터벌 타이머로 f(argp) 실행 시간 추정.
 * n회 실행의 평균을 반환한다.
 */
double ftimer_itimer(ftimer_test_funct f, void *argp, int n)
{
    double start, tmeas;
    int i;

    init_etime();
    start = get_etime();
    for (i = 0; i < n; i++)
	f(argp);
    tmeas = get_etime() - start;
    return tmeas / n;
}

/*
 * ftimer_gettod - gettimeofday로 f(argp) 실행 시간 추정.
 * n회 실행의 평균을 반환한다.
 */
double ftimer_gettod(ftimer_test_funct f, void *argp, int n)
{
    int i;
    struct timeval stv, etv;
    double diff;

    gettimeofday(&stv, NULL);
    for (i = 0; i < n; i++)
	f(argp);
    gettimeofday(&etv,NULL);
    diff = 1E3*(etv.tv_sec - stv.tv_sec) + 1E-3*(etv.tv_usec-stv.tv_usec);
    diff /= n;
    return (1E-3*diff);
}


/*
 * Unix 인터벌 타이머 조작 루틴
 */

/* 인터벌 타이머 초기값 */
#define MAX_ETIME 86400

/* 인터벌 타이머 초기값을 보관하는 정적 변수 */
static struct itimerval first_u; /* 사용자 시간 */
static struct itimerval first_r; /* 실제 시간 */
static struct itimerval first_p; /* 프로파일 시간*/

/* 타이머 초기화 */
static void init_etime(void)
{
    first_u.it_interval.tv_sec = 0;
    first_u.it_interval.tv_usec = 0;
    first_u.it_value.tv_sec = MAX_ETIME;
    first_u.it_value.tv_usec = 0;
    setitimer(ITIMER_VIRTUAL, &first_u, NULL);

    first_r.it_interval.tv_sec = 0;
    first_r.it_interval.tv_usec = 0;
    first_r.it_value.tv_sec = MAX_ETIME;
    first_r.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &first_r, NULL);

    first_p.it_interval.tv_sec = 0;
    first_p.it_interval.tv_usec = 0;
    first_p.it_value.tv_sec = MAX_ETIME;
    first_p.it_value.tv_usec = 0;
    setitimer(ITIMER_PROF, &first_p, NULL);
}

/* init_etime 호출 이후 경과한 실제 시간(초) 반환 */
static double get_etime(void) {
    struct itimerval v_curr;
    struct itimerval r_curr;
    struct itimerval p_curr;

    getitimer(ITIMER_VIRTUAL, &v_curr);
    getitimer(ITIMER_REAL,&r_curr);
    getitimer(ITIMER_PROF,&p_curr);

    return (double) ((first_p.it_value.tv_sec - r_curr.it_value.tv_sec) +
		     (first_p.it_value.tv_usec - r_curr.it_value.tv_usec)*1e-6);
}


