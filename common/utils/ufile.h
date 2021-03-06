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

#ifndef _H4H_FILE_H
#define _H4H_FILE_H

#if defined(KERNEL_MODE)
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

typedef struct file* h4h_file_t;

#elif defined(USER_MODE)
#include <stdint.h>
#include <fcntl.h>

typedef int h4h_file_t;

#else
#error Invalid Platform (KERNEL_MODE or USER_MODE)
#endif

h4h_file_t h4h_fopen (const char* path, int flags, int rights);
void h4h_fclose (h4h_file_t file);
uint64_t h4h_fread (h4h_file_t file, uint64_t offset, uint8_t* data, uint64_t size);
uint64_t h4h_fwrite (h4h_file_t file, uint64_t offset, uint8_t* data, uint64_t size);
uint32_t h4h_fsync (h4h_file_t file);
uint32_t h4h_funlink (h4h_file_t file);
void h4h_flog (const char* filename, char* string);

#endif
