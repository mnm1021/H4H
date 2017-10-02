/*
The MIT License (MIT)

Copyright (c) 2014-2015 CSAIL, MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#ifndef _H4H_DEBUG_H
#define _H4H_DEBUG_H

#if defined(KERNEL_MODE) /* KERNEL_MODE */
#ifdef CONFIG_ENABLE_MSG
	#define h4h_msg(fmt, ...)  \
		do {    \
			printk(KERN_INFO "h4h: " fmt "\n", ##__VA_ARGS__);  \
		} while (0);
	#define h4h_track() \
		do {	\
			printk(KERN_INFO "h4h: [%s:%d]\n", __FUNCTION__, __LINE__); \
		} while (0);
#else
	#define h4h_msg(fmt, ...)
	#define h4h_track()
#endif
#define h4h_warning(fmt, ...)  \
	do {    \
		printk(KERN_ERR "h4h-warning: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__);    \
	} while (0);
#define h4h_error(fmt, ...)  \
	do {    \
		printk(KERN_ERR "h4h-error: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__);    \
	} while (0);


#elif defined(USER_MODE) /* USER_MODE */

#include <stdio.h>

#ifdef CONFIG_ENABLE_MSG
	#define h4h_msg(fmt, ...)  \
		do {    \
			printf("h4h: " fmt "\n", ##__VA_ARGS__);  \
		} while (0);
	#define h4h_track() \
		do {	\
			printf("h4h: [%s:%d]\n", __FUNCTION__, __LINE__); \
		} while (0);
#else
	#define h4h_msg(fmt, ...)
	#define h4h_track()
#endif
#define h4h_warning(fmt, ...)  \
	do {    \
		printf("h4h-warning: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__);    \
	} while (0);
#define h4h_error(fmt, ...)  \
	do {    \
		printf("h4h-error: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__);    \
	} while (0);

#define BUG_ON(a) 
#define WARN_ON(a)

#pragma GCC diagnostic ignored "-Wformat"

#else	/* ERROR */
	#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

/* Platform-independent Code */
#define h4h_warn_on(condition) \
	do { 	\
		if (condition)	\
			h4h_warning ("h4h_warn_on"); \
		WARN_ON(condition); \
	} while (0);


#ifdef CONFIG_ENABLE_DEBUG
	#define h4h_bug_on(condition)  \
		do { 	\
			if (condition)	\
				h4h_error ("h4h_bug_on"); \
			BUG_ON(condition); \
		} while (0);
	#define h4h_dbg_msg(fmt, ...)  \
		do {    \
			printk(KERN_INFO "h4h: " fmt " [%s:%d]\n", ##__VA_ARGS__, __FILE__, __LINE__);    \
		} while (0);
#else
	#define h4h_bug_on(condition)
	#define h4h_dbg_msg(fmt, ...)
#endif

#endif /* _H4H_DEBUG_H */
