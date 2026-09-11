/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** FileX Component                                                       */
/**                                                                       */
/**   Directory                                                           */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/
#define FX_SOURCE_CODE


/* Include necessary system files.  */

#include "fx_api.h"

#ifdef FX_ENABLE_EXFAT
#include "fx_directory.h"

/* NE301 patch: transparent UTF-8 name support on exFAT.
 *
 * FileX 6.1 offers no public API to create or look up exFAT entries by their
 * real (UTF-16) name, so names passed through the CHAR* APIs are zero-
 * extended onto the disk and render as mojibake everywhere except on this
 * device. To interoperate with a PC we convert here: any non-ASCII CHAR*
 * name that is valid UTF-8 is encoded to UTF-16LE and written through the
 * unicode entry writer (which also stores the correct NameHash). Names that
 * are not valid UTF-8 keep the historical zero-extension behavior. The
 * matching read/search sides live in fx_directory_exFAT_entry_read.c and
 * fx_directory_search.c. */

/* Decode UTF-8 into UTF-16LE (with surrogate pairs). Returns the number of
 * UTF-16 code units, or 0 when the input is malformed or does not fit. */
UINT  _fx_utility_exFAT_utf8_to_utf16(const CHAR *utf8, UCHAR *u16, UINT u16_max)
{
UINT n = 0;
const UCHAR *p = (const UCHAR *)utf8;

    while (*p)
    {
        ULONG cp;
        int len, i;
        UCHAR c = *p;

        if ((c & 0x80) == 0)          { cp = c;            len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F;     len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F;     len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07;     len = 4; }
        else
        {
            return(0);
        }

        for (i = 1; i < len; i++)
        {
            if ((p[i] & 0xC0) != 0x80)
            {
                return(0);
            }
            cp = (cp << 6) | (p[i] & 0x3F);
        }

        /* reject overlong forms and UTF-8-encoded surrogates */
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        {
            return(0);
        }

        if (cp >= 0x10000)
        {
            if (n + 2 > u16_max)
            {
                return(0);
            }
            cp -= 0x10000;
            u16[n * 2]      = (UCHAR)((cp >> 10) & 0xFF);
            u16[n * 2 + 1]  = (UCHAR)((((cp >> 10) >> 8) & 0x03) | 0xD8);
            n++;
            u16[n * 2]      = (UCHAR)(cp & 0xFF);
            u16[n * 2 + 1]  = (UCHAR)(((cp & 0x3FF) >> 8) | 0xDC);
            n++;
        }
        else
        {
            if (n + 1 > u16_max)
            {
                return(0);
            }
            u16[n * 2]      = (UCHAR)(cp & 0xFF);
            u16[n * 2 + 1]  = (UCHAR)(cp >> 8);
            n++;
        }
        p += len;
    }
    return(n);
}


/**************************************************************************/
/*                                                                        */
/*  FUNCTION                                               RELEASE        */
/*                                                                        */
/*    _fx_directory_exFAT_entry_write                     PORTABLE C      */
/*                                                           6.1          */
/*  AUTHOR                                                                */
/*                                                                        */
/*    William E. Lamie, Microsoft Corporation                             */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This function writes the supplied directory entry to the specified  */
/*    logical sector and offset.                                          */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    media_ptr                             Media control block pointer   */
/*    entry_ptr                             Pointer to directory entry    */
/*    update_level                          Update level for entry write  */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    return status                                                       */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    _fx_directory_exFAT_unicode_entry_write                             */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    fx_directory_attributes_set                                         */
/*    fx_directory_create                                                 */
/*    fx_directory_delete                                                 */
/*    fx_directory_exFAT_unicode_entry_write                              */
/*    fx_directory_rename                                                 */
/*    fx_file_allocate                                                    */
/*    fx_file_attributes_set                                              */
/*    fx_file_best_effort_allocate                                        */
/*    fx_file_close                                                       */
/*    fx_file_create                                                      */
/*    fx_file_date_time_set                                               */
/*    fx_file_delete                                                      */
/*    fx_file_rename                                                      */
/*    fx_file_truncate_release                                            */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  05-19-2020     William E. Lamie         Initial Version 6.0           */
/*  09-30-2020     William E. Lamie         Modified comment(s),          */
/*                                            resulting in version 6.1    */
/*                                                                        */
/**************************************************************************/
UINT  _fx_directory_exFAT_entry_write(FX_MEDIA *media_ptr, FX_DIR_ENTRY *entry_ptr, UCHAR update_level)
{

UINT status;


    /* NE301 patch: a non-ASCII CHAR* name that is valid UTF-8 is written as
     * its real UTF-16 name so a PC card reader shows the proper characters.
     * Every update level is redirected for a consistent name encoding: the
     * unicode writer only touches the name area at UPDATE_FULL/UPDATE_NAME,
     * so STREAM/FILE/DELETE levels behave identically either way. ASCII
     * names and malformed byte sequences keep the legacy zero-extended
     * path. */
    if (entry_ptr -> fx_dir_entry_name)
    {
        const UCHAR *p = (const UCHAR *)entry_ptr -> fx_dir_entry_name;
        for (; *p; p++)
        {
            if (*p > 0x7F)
            {
                UCHAR u16[FX_MAX_LONG_NAME_LEN * 2];
                UINT  u16_len = _fx_utility_exFAT_utf8_to_utf16(entry_ptr -> fx_dir_entry_name, u16, FX_MAX_LONG_NAME_LEN);
                if (u16_len)
                {
                    /* NE301 patch: on read-back the name is re-encoded to
                     * the exact same UTF-8 bytes, so a name of
                     * FX_MAX_LONG_NAME_LEN-1 bytes or more would be truncated
                     * in the CHAR* buffer - an entry the device itself can
                     * neither list nor open. Reject it at write time instead
                     * of leaving an invisible entry on the disk. */
                    const UCHAR *q = (const UCHAR *)entry_ptr -> fx_dir_entry_name;
                    UINT  nlen = 0;
                    while (q[nlen])
                    {
                        nlen++;
                    }
                    if (nlen >= FX_MAX_LONG_NAME_LEN - 1)
                    {
                        return(FX_INVALID_NAME);
                    }

                    status = _fx_directory_exFAT_unicode_entry_write(media_ptr, entry_ptr, update_level, (USHORT *)u16, u16_len);
                    return(status);
                }
                break;
            }
        }
    }

    /* Call the unicode director entry write function.  */
    status =  _fx_directory_exFAT_unicode_entry_write(media_ptr, entry_ptr, update_level, NULL, 0);

    /* Return completion status.  */
    return(status);
}

#endif

