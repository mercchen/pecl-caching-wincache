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
   | Module: wincache_utils.c                                                                     |
   +----------------------------------------------------------------------------------------------+
   | Author: Kanwaljeet Singla <ksingla@microsoft.com>                                            |
   +----------------------------------------------------------------------------------------------+
*/

#include "precomp.h"

#define DWORD_MAX    0xFFFFFFFF

static unsigned int crc32_generate(int n);
static unsigned int utils_crc32(const char * str, unsigned int strlen);

/* CRC table generated by crc32_generate() */
static unsigned int crc32_table[] = {
    /*   0 */  0x00000000, 0x3b83984b, 0x77073096, 0x4c84a8dd,
    /*   4 */  0xee0e612c, 0xd58df967, 0x990951ba, 0xa28ac9f1,
    /*   8 */  0x076dc419, 0x3cee5c52, 0x706af48f, 0x4be96cc4,
    /*  12 */  0xe963a535, 0xd2e03d7e, 0x9e6495a3, 0xa5e70de8,
    /*  16 */  0x0edb8832, 0x35581079, 0x79dcb8a4, 0x425f20ef,
    /*  20 */  0xe0d5e91e, 0xdb567155, 0x97d2d988, 0xac5141c3,
    /*  24 */  0x09b64c2b, 0x3235d460, 0x7eb17cbd, 0x4532e4f6,
    /*  28 */  0xe7b82d07, 0xdc3bb54c, 0x90bf1d91, 0xab3c85da,
    /*  32 */  0x1db71064, 0x2634882f, 0x6ab020f2, 0x5133b8b9,
    /*  36 */  0xf3b97148, 0xc83ae903, 0x84be41de, 0xbf3dd995,
    /*  40 */  0x1adad47d, 0x21594c36, 0x6ddde4eb, 0x565e7ca0,
    /*  44 */  0xf4d4b551, 0xcf572d1a, 0x83d385c7, 0xb8501d8c,
    /*  48 */  0x136c9856, 0x28ef001d, 0x646ba8c0, 0x5fe8308b,
    /*  52 */  0xfd62f97a, 0xc6e16131, 0x8a65c9ec, 0xb1e651a7,
    /*  56 */  0x14015c4f, 0x2f82c404, 0x63066cd9, 0x5885f492,
    /*  60 */  0xfa0f3d63, 0xc18ca528, 0x8d080df5, 0xb68b95be,
    /*  64 */  0x3b6e20c8, 0x00edb883, 0x4c69105e, 0x77ea8815,
    /*  68 */  0xd56041e4, 0xeee3d9af, 0xa2677172, 0x99e4e939,
    /*  72 */  0x3c03e4d1, 0x07807c9a, 0x4b04d447, 0x70874c0c,
    /*  76 */  0xd20d85fd, 0xe98e1db6, 0xa50ab56b, 0x9e892d20,
    /*  80 */  0x35b5a8fa, 0x0e3630b1, 0x42b2986c, 0x79310027,
    /*  84 */  0xdbbbc9d6, 0xe038519d, 0xacbcf940, 0x973f610b,
    /*  88 */  0x32d86ce3, 0x095bf4a8, 0x45df5c75, 0x7e5cc43e,
    /*  92 */  0xdcd60dcf, 0xe7559584, 0xabd13d59, 0x9052a512,
    /*  96 */  0x26d930ac, 0x1d5aa8e7, 0x51de003a, 0x6a5d9871,
    /* 100 */  0xc8d75180, 0xf354c9cb, 0xbfd06116, 0x8453f95d,
    /* 104 */  0x21b4f4b5, 0x1a376cfe, 0x56b3c423, 0x6d305c68,
    /* 108 */  0xcfba9599, 0xf4390dd2, 0xb8bda50f, 0x833e3d44,
    /* 112 */  0x2802b89e, 0x138120d5, 0x5f058808, 0x64861043,
    /* 116 */  0xc60cd9b2, 0xfd8f41f9, 0xb10be924, 0x8a88716f,
    /* 120 */  0x2f6f7c87, 0x14ece4cc, 0x58684c11, 0x63ebd45a,
    /* 124 */  0xc1611dab, 0xfae285e0, 0xb6662d3d, 0x8de5b576,
    /* 128 */  0x76dc4190, 0x4d5fd9db, 0x01db7106, 0x3a58e94d,
    /* 132 */  0x98d220bc, 0xa351b8f7, 0xefd5102a, 0xd4568861,
    /* 136 */  0x71b18589, 0x4a321dc2, 0x06b6b51f, 0x3d352d54,
    /* 140 */  0x9fbfe4a5, 0xa43c7cee, 0xe8b8d433, 0xd33b4c78,
    /* 144 */  0x7807c9a2, 0x438451e9, 0x0f00f934, 0x3483617f,
    /* 148 */  0x9609a88e, 0xad8a30c5, 0xe10e9818, 0xda8d0053,
    /* 152 */  0x7f6a0dbb, 0x44e995f0, 0x086d3d2d, 0x33eea566,
    /* 156 */  0x91646c97, 0xaae7f4dc, 0xe6635c01, 0xdde0c44a,
    /* 160 */  0x6b6b51f4, 0x50e8c9bf, 0x1c6c6162, 0x27eff929,
    /* 164 */  0x856530d8, 0xbee6a893, 0xf262004e, 0xc9e19805,
    /* 168 */  0x6c0695ed, 0x57850da6, 0x1b01a57b, 0x20823d30,
    /* 172 */  0x8208f4c1, 0xb98b6c8a, 0xf50fc457, 0xce8c5c1c,
    /* 176 */  0x65b0d9c6, 0x5e33418d, 0x12b7e950, 0x2934711b,
    /* 180 */  0x8bbeb8ea, 0xb03d20a1, 0xfcb9887c, 0xc73a1037,
    /* 184 */  0x62dd1ddf, 0x595e8594, 0x15da2d49, 0x2e59b502,
    /* 188 */  0x8cd37cf3, 0xb750e4b8, 0xfbd44c65, 0xc057d42e,
    /* 192 */  0x4db26158, 0x7631f913, 0x3ab551ce, 0x0136c985,
    /* 196 */  0xa3bc0074, 0x983f983f, 0xd4bb30e2, 0xef38a8a9,
    /* 200 */  0x4adfa541, 0x715c3d0a, 0x3dd895d7, 0x065b0d9c,
    /* 204 */  0xa4d1c46d, 0x9f525c26, 0xd3d6f4fb, 0xe8556cb0,
    /* 208 */  0x4369e96a, 0x78ea7121, 0x346ed9fc, 0x0fed41b7,
    /* 212 */  0xad678846, 0x96e4100d, 0xda60b8d0, 0xe1e3209b,
    /* 216 */  0x44042d73, 0x7f87b538, 0x33031de5, 0x088085ae,
    /* 220 */  0xaa0a4c5f, 0x9189d414, 0xdd0d7cc9, 0xe68ee482,
    /* 224 */  0x5005713c, 0x6b86e977, 0x270241aa, 0x1c81d9e1,
    /* 228 */  0xbe0b1010, 0x8588885b, 0xc90c2086, 0xf28fb8cd,
    /* 232 */  0x5768b525, 0x6ceb2d6e, 0x206f85b3, 0x1bec1df8,
    /* 236 */  0xb966d409, 0x82e54c42, 0xce61e49f, 0xf5e27cd4,
    /* 240 */  0x5edef90e, 0x655d6145, 0x29d9c998, 0x125a51d3,
    /* 244 */  0xb0d09822, 0x8b530069, 0xc7d7a8b4, 0xfc5430ff,
    /* 248 */  0x59b33d17, 0x6230a55c, 0x2eb40d81, 0x153795ca,
    /* 252 */  0xb7bd5c3b, 0x8c3ec470, 0xc0ba6cad, 0xfb39f4e6,
};

static unsigned int crc32_generate(int n)
{
    int i            = 0;
    unsigned int crc = 0;

    crc = n;
    for(i = 8; i >= 0; i--)
    {
        if(crc & 1)
        {
            crc = (crc >> 1) ^ 0xEDB88320;
        }
        else
        {
            crc >>= 1;
        }
    }

    return crc;
}

static unsigned int utils_crc32(const char * str, unsigned int strlen)
{
    unsigned int index       = 0;
    unsigned int table_index = 0;
    unsigned int crcvalue    = 0xFFFFFFFF;
    char         chvalue     = 0;
    char         toldiff     = 'a' - 'A';

    dprintdecorate("start utils_crc32");

    for(index = 0; index < strlen; index++)
    {
        chvalue = str[index];
        if(chvalue >= 'A' && chvalue <= 'Z')
        {
            chvalue = chvalue + toldiff;
        }

        table_index = (crcvalue ^ chvalue) & 0x000000FF;
        crcvalue = ((crcvalue >> 8) & 0x00FFFFFF) ^ crc32_table[table_index];
    }

    dprintdecorate("end utils_crc32");

    return ~crcvalue;
}

unsigned int utils_hashcalc(const char * str, unsigned int strlen)
{
    return utils_crc32(str, strlen);
}

unsigned int utils_getindex(const char * filename, unsigned int numfiles)
{
    unsigned int hash   = 0;
    unsigned int length = 0;

    dprintdecorate("start utils_getindex");

    _ASSERT(filename != NULL);
    _ASSERT(numfiles != 0);

    length = strlen(filename);
    _ASSERT(length != 0);

    hash = utils_hashcalc(filename, length);
    hash = hash % numfiles;

    dprintdecorate("end utils_getindex");

    return hash;
}

const char * utils_filepath(zend_file_handle * file_handle)
{
    const char * pchar = NULL;

    dprintverbose("start utils_filepath");
    _ASSERT(file_handle != NULL);

    /* Use filename if opened_path is null */
    if(file_handle->opened_path != NULL)
    {
        pchar = file_handle->opened_path;
    }
    else if(file_handle->filename != NULL)
    {
        pchar = file_handle->filename;
    }

    dprintverbose("end utils_filepath");
    return pchar;
}

char * utils_fullpath(const char * filename)
{
    char *       filepath = NULL;
    unsigned int fplength = 0;
    unsigned int index    = 0;

    dprintverbose("start utils_fullpath");
    _ASSERT(filename != NULL);

    /* Get fullpath in a standardized format */
    filepath = alloc_emalloc(MAX_PATH);
    if(filepath == NULL)
    {
        goto Finished;
    }

    ZeroMemory(filepath, MAX_PATH);
    fplength = GetFullPathName(filename, MAX_PATH, filepath, NULL);
    if(!fplength)
    {
        /* Return the filename which was passed */
        strcpy(filepath, filename);
    }

    for(index = 0; index < fplength; index++)
    {
        if(filepath[index] == '/')
        {
            filepath[index] = '\\';
        }
    }

Finished:

    dprintverbose("end utils_fullpath");

    return filepath;
}

int utils_cwdcexec(char * buffer, unsigned int length TSRMLS_DC)
{
    int          result   = NONFATAL;
    unsigned int cwdlen   = 0;
    const char * execname = NULL;
    unsigned int execlen  = 0;

    dprintverbose("start utils_cwdcxec");

    _ASSERT(buffer != NULL);
    _ASSERT(length >  MAX_PATH);

    ZeroMemory(buffer, length);

    cwdlen = GetCurrentDirectory(length, buffer);
    *(buffer + cwdlen) = '|';
    
    if(zend_is_executing(TSRMLS_C))
    {
        execname = zend_get_executed_filename(TSRMLS_C);
        execlen = strlen(execname);
        
        if((length - cwdlen - 2) < execlen)
        {
            result = FATAL_NEED_MORE_MEMORY;
            goto Finished;
        }

        strncpy(buffer + cwdlen + 1, execname, execlen);
    }

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in utils_cwdcxec", result);
        _ASSERT(result > WARNING_COMMON_BASE);
    }

    dprintverbose("end utils_cwdcexec");

    return result;
}

int utils_filefolder(const char * filepath, unsigned int flength, char * pbuffer, unsigned int length)
{
    int    result  = NONFATAL;
    char * pbslash = NULL;

    _ASSERT(filepath != NULL);
    _ASSERT(pbuffer  != NULL);
    _ASSERT(IS_ABSOLUTE_PATH(filepath, flength));

    if(length < flength)
    {
        result = FATAL_NEED_MORE_MEMORY;
        goto Finished;
    }

    ZeroMemory(pbuffer, length);

    pbslash = strrchr(filepath, '\\');
    _ASSERT(pbslash != NULL);

    /* length does not include backslash */
    length = pbslash - filepath;
    memcpy_s(pbuffer, length, filepath, length);

Finished:

    if(FAILED(result))
    {
        dprintimportant("failure %d in utils_filefolder", result);
        _ASSERT(result > WARNING_COMMON_BASE);
    }

    return result;
}

int utils_apoolpid(TSRMLS_D)
{
    int          retval = -1;
    char *       buffer = NULL;
    unsigned int buflen = 0;

    dprintverbose("start utils_apoolpid");

    if(WCG(apppoolid) != NULL)
    {
        dprintverbose("using apppoolid");
        retval = utils_hashcalc(WCG(apppoolid), strlen(WCG(apppoolid)));
    } else {
        buflen = GetEnvironmentVariable("APP_POOL_ID", NULL, 0);
        if(buflen == 0)
        {
            goto Finished;
        }

        buffer = (char *)alloc_pemalloc(buflen);
        if(buffer == NULL)
        {
            goto Finished;
        }

        buflen = GetEnvironmentVariable("APP_POOL_ID", buffer, buflen);
        if(buflen == 0)
        {
            goto Finished;
        }

        /* Keeping number between 99999 and 999999 to not confuse with regular pids */
        /* 999999 - 100000 = 899999. Code dependent on assumption that apoolpid > 99999 */
        /* If hashcalc returned a -ve value, make it +ve by subtracting from 0 */
        retval = utils_hashcalc(buffer, buflen);
    }

    retval %= 899999;
    retval = ((retval < 0) ? (0 - retval) : retval);
    retval += 100000;

Finished:

    if(buffer != NULL)
    {
        alloc_pefree(buffer);
        buffer = NULL;
    }

    dprintverbose("end utils_apoolpid");

    return retval;
}

unsigned int utils_ticksdiff(unsigned int present, unsigned int past)
{
    unsigned int ticksdiff = 0;

    _ASSERT(past != 0);

    /* If present is 0, get current tickcount */
    if(present == 0)
    {
        present = GetTickCount();
    }

    /* Take care of rollover while calculating difference */
    if(present >= past)
    {
        ticksdiff = present - past;
    }
    else
    {
        ticksdiff = (DWORD_MAX - past) + present;
    }

    return ticksdiff;
}

/* Copy of php_resolve_path from PHP 5.3 branch for use in PHP 5.2 */
char * utils_resolve_path(const char *filename, int filename_length, const char *path TSRMLS_DC)
{
    char resolved_path[MAXPATHLEN];
    char trypath[MAXPATHLEN];
    const char *ptr, *end, *p;
    char *actual_path;
    php_stream_wrapper *wrapper;

    if (!filename) {
        return NULL;
    }

    /* Don't resolve paths which contain protocol (except of file://) */
    for (p = filename; isalnum((int)*p) || *p == '+' || *p == '-' || *p == '.'; p++);
    if ((*p == ':') && (p - filename > 1) && (p[1] == '/') && (p[2] == '/')) {
        wrapper = php_stream_locate_url_wrapper(filename, &actual_path, STREAM_OPEN_FOR_INCLUDE TSRMLS_CC);
        if (wrapper == &php_plain_files_wrapper) {
            if (tsrm_realpath(actual_path, resolved_path TSRMLS_CC)) {
                return alloc_estrdup(resolved_path);
            }
        }
        return NULL;
    }

    /*
     * if filename starts with: "." or "..\"
     * --OR--
     * if filename starts with: "X:" or "\\"
     * --OR--
     * if filename starts with: "\" (root on current drive)
     * --OR--
     * path is NULL or empty
     * --THEN--
     * this is an absolute path.
     */
    if ((*filename == '.' && 
         (IS_SLASH(filename[1]) || 
          ((filename[1] == '.') && IS_SLASH(filename[2])))) ||
         (IS_ABSOLUTE_PATH(filename, filename_length) || IS_SLASH(filename[0])) ||
        !path ||
        !*path) {
        if (tsrm_realpath(filename, resolved_path TSRMLS_CC)) {
            return alloc_estrdup(resolved_path);
        } else {
            return NULL;
        }
    }

    ptr = path;
    while (ptr && *ptr) {
        /* Check for stream wrapper */
        int is_stream_wrapper = 0;

        for (p = ptr; isalnum((int)*p) || *p == '+' || *p == '-' || *p == '.'; p++);
        if ((*p == ':') && (p - ptr > 1) && (p[1] == '/') && (p[2] == '/')) {
            /* .:// or ..:// is not a stream wrapper */
            if (p[-1] != '.' || p[-2] != '.' || p - 2 != ptr) {
                p += 3;
                is_stream_wrapper = 1;
            }
        }
        end = strchr(p, DEFAULT_DIR_SEPARATOR);
        if (end) {
            if ((end-ptr) + 1 + filename_length + 1 >= MAXPATHLEN) {
                ptr = end + 1;
                continue;
            }
            memcpy(trypath, ptr, end-ptr);
            trypath[end-ptr] = '/';
            memcpy(trypath+(end-ptr)+1, filename, filename_length+1);
            ptr = end+1;
        } else {
            int len = strlen(ptr);

            if (len + 1 + filename_length + 1 >= MAXPATHLEN) {
                break;
            }
            memcpy(trypath, ptr, len);
            trypath[len] = '/';
            memcpy(trypath+len+1, filename, filename_length+1);
            ptr = NULL;
        }
        actual_path = trypath;
        if (is_stream_wrapper) {
            wrapper = php_stream_locate_url_wrapper(trypath, &actual_path, STREAM_OPEN_FOR_INCLUDE TSRMLS_CC);
            if (!wrapper) {
                continue;
            } else if (wrapper != &php_plain_files_wrapper) {
                if (wrapper->wops->url_stat) {
                    php_stream_statbuf ssb;

                    if (SUCCESS == wrapper->wops->url_stat(wrapper, trypath, 0, &ssb, NULL TSRMLS_CC)) {
                        return alloc_estrdup(trypath);
                    }
                }
                continue;
            }
        }
        if (tsrm_realpath(actual_path, resolved_path TSRMLS_CC)) {
            return alloc_estrdup(resolved_path);
        }
    } /* end provided path */

    /* check in calling scripts' current working directory as a fall back case
     */
    if (zend_is_executing(TSRMLS_C)) {
        const char *exec_fname = zend_get_executed_filename(TSRMLS_C);
        int exec_fname_length = strlen(exec_fname);

        while ((--exec_fname_length >= 0) && !IS_SLASH(exec_fname[exec_fname_length]));
        if (exec_fname && exec_fname[0] != '[' &&
            exec_fname_length > 0 &&
            exec_fname_length + 1 + filename_length + 1 < MAXPATHLEN) {
            memcpy(trypath, exec_fname, exec_fname_length + 1);
            memcpy(trypath+exec_fname_length + 1, filename, filename_length+1);
            actual_path = trypath;

            /* Check for stream wrapper */
            for (p = trypath; isalnum((int)*p) || *p == '+' || *p == '-' || *p == '.'; p++);
            if ((*p == ':') && (p - trypath > 1) && (p[1] == '/') && (p[2] == '/')) {
                wrapper = php_stream_locate_url_wrapper(trypath, &actual_path, STREAM_OPEN_FOR_INCLUDE TSRMLS_CC);
                if (!wrapper) {
                    return NULL;
                } else if (wrapper != &php_plain_files_wrapper) {
                    if (wrapper->wops->url_stat) {
                        php_stream_statbuf ssb;

                        if (SUCCESS == wrapper->wops->url_stat(wrapper, trypath, 0, &ssb, NULL TSRMLS_CC)) {
                            return alloc_estrdup(trypath);
                        }
                    }
                    return NULL;
                }
            }

            if (tsrm_realpath(actual_path, resolved_path TSRMLS_CC)) {
                return alloc_estrdup(resolved_path);
            }
        }
    }

    return NULL;
}

#if (defined(_MSC_VER) && (_MSC_VER < 1500))
int wincache_php_snprintf_s(char *buf, size_t len, size_t len2, const char *format,...)
{
    va_list arglist;
    int     retval = 0;

    va_start(arglist, format);
    retval = ap_php_snprintf(buf, len, format, arglist);
    va_end(arglist);
    
    return retval;
}
#endif
