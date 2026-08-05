/***************************************************************************/ /**
 * @file sys.h
 * @brief BSD socket sys.h override for newlib / STM32 host (timespec provided by libc).
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 ******************************************************************************/

#ifndef _SYS_H
#define _SYS_H

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#ifndef size_t
typedef unsigned int size_t;
#endif
#endif /* _SIZE_T_DEFINED */

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED

#if !defined(__ssize_t_defined) && !defined(_SSIZE_T_DECLARED)
#define __ssize_t_defined
#define _SSIZE_T_DECLARED
typedef int ssize_t;
#endif /* _SSIZE_T_DEFINED */
#endif

#ifndef _SYS__SIGSET_H_
#define	_SYS__SIGSET_H_
#ifndef __sigset_t_defined
#define __sigset_t_defined
typedef unsigned long __sigset_t;
#endif /* __sigset_t_defined */
#endif /* !_SYS__SIGSET_H_ */

#ifndef _SUSECONDS_T_DECLARED
#define	_SUSECONDS_T_DECLARED
typedef	long suseconds_t;
#endif

#if !defined(_TIME_T_DEFINED) && !defined(_TIME_T_DECLARED)
typedef	long int time_t;
#define	_TIME_T_DEFINED
#define	_TIME_T_DECLARED
#endif

#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
#if !defined(__USE_POSIX) && !defined(_STRUCT_TIMEVAL)
#define _STRUCT_TIMEVAL
#ifndef __struct_timeval_defined
#define __struct_timeval_defined
struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
};
#endif /* __struct_timeval_defined */
#endif /* _STRUCT_TIMEVAL */
#endif

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
#if !defined(__USE_POSIX) && !defined(_STRUCT_TIMESPEC)
#define _STRUCT_TIMEVAL
#ifndef __struct_timespec_defined
#define __struct_timespec_defined
/* struct timespec provided by newlib on STM32 host */
#endif /* __struct_timespec_defined */
#endif
#endif

#endif /* _SYS_H */
