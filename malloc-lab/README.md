#####################################################################
# CS:APP Malloc Lab
# 학생용 배포 파일
#
# Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
# May not be used, modified, or copied without permission.
#
######################################################################

***********
주요 파일:
***********

mm.{c,h}	
	여러분이 구현할 malloc 패키지입니다. 제출하는 파일은 mm.c이며,
	수정해도 되는 파일도 mm.c뿐입니다.

mdriver.c	
	mm.c를 검사하는 malloc 드라이버입니다.

short{1,2}-bal.rep
	시작할 때 쓰기 좋은 아주 작은 trace 파일 두 개입니다.

Makefile	
	드라이버를 빌드합니다.

**********************************
드라이버용 기타 지원 파일
**********************************

config.h	malloc lab 드라이버 설정
fsecs.{c,h}	서로 다른 타이머 패키지를 감싸는 래퍼
clock.{c,h}	Pentium·Alpha 사이클 카운터 접근 루틴
fcyc.{c,h}	사이클 카운터 기반 타이머 함수
ftimer.{c,h}	인터벌 타이머·gettimeofday() 기반 타이머 함수
memlib.{c,h}	힙과 sbrk 함수를 시뮬레이션

*******************************
드라이버 빌드 및 실행
*******************************
빌드하려면 셸에서 `make`를 입력하세요.

아주 작은 trace로 드라이버를 실행하려면:

	unix> mdriver -V -f short1-bal.rep

`-V` 옵션은 디버깅에 도움이 되는 추적·요약 정보를 출력합니다.

드라이버 플래그 목록을 보려면:

	unix> mdriver -h

