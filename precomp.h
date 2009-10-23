/*
   +----------------------------------------------------------------------------------------------+
   | Windows Cache for PHP                                                                        |
   +----------------------------------------------------------------------------------------------+
   | Copyright (c) 2009, Microsoft Corporation. All rights reserved.                              |
   |                                                                                              |
   | Redistribution and use in source and binary forms, with or without modification, are         |
   | permitted provided that the following conditions are met:                                    |
   | - Redistributions of source code must retain the above copyright notice, this list of        |
   | conditions and the following disclaimer.                                                     |
   | - Redistributions in binary form must reproduce the above copyright notice, this list of     |
   | conditions and the following disclaimer in the documentation and/or other materials provided |
   | with the distribution.                                                                       |
   | - Neither the name of the Microsoft Corporation nor the names of its contributors may be     |
   | used to endorse or promote products derived from this software without specific prior written|
   | permission.                                                                                  |
   |                                                                                              |
   | THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS  |
   | OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF              |
   | MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE   |
   | COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,    |
   | EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE|
   | GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED   |
   | AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING    |
   | NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED |
   | OF THE POSSIBILITY OF SUCH DAMAGE.                                                           |
   +----------------------------------------------------------------------------------------------+
   | Module: precomp.h                                                                            |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   +----------------------------------------------------------------------------------------------+
*/

#ifndef _PRECOMP_H_
#define _PRECOMP_H_

#define PHP_WINCACHE_EXTNAME   "wincache"
#define PHP_WINCACHE_EXTVER    "1.0.1012.0"

/* comment following line for release builds */
//#define WINCACHE_DEBUG

#ifdef PHP_WIN32
 #define PHP_WINCACHE_API __declspec(dllexport)
#else
 #define PHP_WINCACHE_API
#endif

#ifdef HAVE_CONFIG_H
 #include "config.h"
#endif

#ifdef ZTS
 #include "TSRM.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "sapi.h"
#include "ext/standard/info.h"
#include "zend_extensions.h"

#include <tlhelp32.h>

#define ALIGNDWORD(size)   ((size % 4) ? (size+(4-(size % 4))) : (size))
#define ALIGNQWORD(size)   ((size % 8) ? (size+(8-(size % 8))) : (size))

#if PHP_VERSION_ID >= 60000
 #define PHP_VERSION_60
#elif PHP_VERSION_ID >= 50300
 #define PHP_VERSION_53
#elif PHP_VERSION_ID >= 50200
 #define PHP_VERSION_52
#endif

#ifdef _ASSERT
 #undef _ASSERT
#endif

#ifdef WINCACHE_DEBUG
 #define WINCACHE_TEST
 #define _ASSERT(x)   if(!(x)) { dprintalways(#x); if(IsDebuggerPresent()) { DebugBreak(); } }
#else
 #define _ASSERT(x)
#endif

#if (defined(_MSC_VER) && (_MSC_VER < 1500))
 #define memcpy_s(dst, size, src, cnt) memcpy(dst, src, cnt)
 #define sprintf_s(buffer, size, format) sprintf(buffer, format)
 #define _snprintf_s(buffer, s1, s2, format, va_alist) snprintf(buffer, s1, format, va_alist)
 #define vsprintf_s(buffer, size, format, va_alist) vsprintf(buffer, format, va_alist)
 #define strcpy_s(src, size, dst) strcpy(src, dst)
#endif

#define CACHE_TYPE_FILELIST        1
#define CACHE_TYPE_RELPATHS        2
#define CACHE_TYPE_FILECONTENT     3
#define CACHE_TYPE_BYTECODES       4

#include "wincache_debug.h"
#include "wincache_utils.h"
#include "wincache_error.h"
#include "wincache_lock.h"
#include "wincache_filemap.h"
#include "wincache_alloc.h"
#include "wincache_fcache.h"
#include "wincache_ocache.h"
#include "wincache_rplist.h"
#include "wincache_aplist.h"
#include "wincache_opcopy.h"
#include "php_wincache.h"

#endif /* _PRECOMP_H_ */