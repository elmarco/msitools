/*
 * String Table Functions
 *
 * Copyright 2002-2004, Mike McCormack for CodeWeavers
 * Copyright 2007 Robert Shearman for CodeWeavers
 * Copyright 2010 Hans Leidekker for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include <stdarg.h>
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "debug.h"
#include "unicode.h"
#include "libmsi.h"
#include "objbase.h"
#include "objidl.h"
#include "msipriv.h"
#include "winnls.h"

#include "query.h"


struct msistring
{
    uint16_t persistent_refcount;
    uint16_t nonpersistent_refcount;
    WCHAR *str;
};

struct string_table
{
    unsigned maxcount;         /* the number of strings */
    unsigned freeslot;
    unsigned codepage;
    unsigned sortcount;
    struct msistring *strings; /* an array of strings */
    unsigned *sorted;              /* index */
};

static bool validate_codepage( unsigned codepage )
{
    if (codepage != CP_ACP && !IsValidCodePage( codepage ))
    {
        WARN("invalid codepage %u\n", codepage);
        return false;
    }
    return true;
}

static string_table *init_stringtable( int entries, unsigned codepage )
{
    string_table *st;

    if (!validate_codepage( codepage ))
        return NULL;

    st = msi_alloc( sizeof (string_table) );
    if( !st )
        return NULL;    
    if( entries < 1 )
        entries = 1;

    st->strings = msi_alloc_zero( sizeof(struct msistring) * entries );
    if( !st->strings )
    {
        msi_free( st );
        return NULL;    
    }

    st->sorted = msi_alloc( sizeof (unsigned) * entries );
    if( !st->sorted )
    {
        msi_free( st->strings );
        msi_free( st );
        return NULL;
    }

    st->maxcount = entries;
    st->freeslot = 1;
    st->codepage = codepage;
    st->sortcount = 0;

    return st;
}

VOID msi_destroy_stringtable( string_table *st )
{
    unsigned i;

    for( i=0; i<st->maxcount; i++ )
    {
        if( st->strings[i].persistent_refcount ||
            st->strings[i].nonpersistent_refcount )
            msi_free( st->strings[i].str );
    }
    msi_free( st->strings );
    msi_free( st->sorted );
    msi_free( st );
}

static int st_find_free_entry( string_table *st )
{
    unsigned i, sz, *s;
    struct msistring *p;

    TRACE("%p\n", st);

    if( st->freeslot )
    {
        for( i = st->freeslot; i < st->maxcount; i++ )
            if( !st->strings[i].persistent_refcount &&
                !st->strings[i].nonpersistent_refcount )
                return i;
    }
    for( i = 1; i < st->maxcount; i++ )
        if( !st->strings[i].persistent_refcount &&
            !st->strings[i].nonpersistent_refcount )
            return i;

    /* dynamically resize */
    sz = st->maxcount + 1 + st->maxcount/2;
    p = msi_realloc_zero( st->strings, sz * sizeof(struct msistring) );
    if( !p )
        return -1;

    s = msi_realloc( st->sorted, sz*sizeof(unsigned) );
    if( !s )
    {
        msi_free( p );
        return -1;
    }

    st->strings = p;
    st->sorted = s;

    st->freeslot = st->maxcount;
    st->maxcount = sz;
    if( st->strings[st->freeslot].persistent_refcount ||
        st->strings[st->freeslot].nonpersistent_refcount )
        ERR("oops. expected freeslot to be free...\n");
    return st->freeslot;
}

static int find_insert_index( const string_table *st, unsigned string_id )
{
    int i, c, low = 0, high = st->sortcount - 1;

    while (low <= high)
    {
        i = (low + high) / 2;
        c = strcmpW( st->strings[string_id].str, st->strings[st->sorted[i]].str );

        if (c < 0)
            high = i - 1;
        else if (c > 0)
            low = i + 1;
        else
            return -1; /* already exists */
    }
    return high + 1;
}

static void insert_string_sorted( string_table *st, unsigned string_id )
{
    int i;

    i = find_insert_index( st, string_id );
    if (i == -1)
        return;

    memmove( &st->sorted[i] + 1, &st->sorted[i], (st->sortcount - i) * sizeof(unsigned) );
    st->sorted[i] = string_id;
    st->sortcount++;
}

static void set_st_entry( string_table *st, unsigned n, WCHAR *str, uint16_t refcount, enum StringPersistence persistence )
{
    if (persistence == StringPersistent)
    {
        st->strings[n].persistent_refcount = refcount;
        st->strings[n].nonpersistent_refcount = 0;
    }
    else
    {
        st->strings[n].persistent_refcount = 0;
        st->strings[n].nonpersistent_refcount = refcount;
    }

    st->strings[n].str = str;

    insert_string_sorted( st, n );

    if( n < st->maxcount )
        st->freeslot = n + 1;
}

static unsigned msi_string2idA( const string_table *st, const char *buffer, unsigned *id )
{
    unsigned sz;
    unsigned r = ERROR_INVALID_PARAMETER;
    WCHAR *str;

    TRACE("Finding string %s in string table\n", debugstr_a(buffer) );

    if( buffer[0] == 0 )
    {
        *id = 0;
        return ERROR_SUCCESS;
    }

    sz = MultiByteToWideChar( st->codepage, 0, buffer, -1, NULL, 0 );
    if( sz <= 0 )
        return r;
    str = msi_alloc( sz*sizeof(WCHAR) );
    if( !str )
        return ERROR_NOT_ENOUGH_MEMORY;
    MultiByteToWideChar( st->codepage, 0, buffer, -1, str, sz );

    r = msi_string2idW( st, str, id );
    msi_free( str );

    return r;
}

static int msi_addstring( string_table *st, unsigned n, const char *data, int len, uint16_t refcount, enum StringPersistence persistence )
{
    WCHAR *str;
    int sz;

    if( !data )
        return 0;
    if( !data[0] )
        return 0;
    if( n > 0 )
    {
        if( st->strings[n].persistent_refcount ||
            st->strings[n].nonpersistent_refcount )
            return -1;
    }
    else
    {
        if( ERROR_SUCCESS == msi_string2idA( st, data, &n ) )
        {
            if (persistence == StringPersistent)
                st->strings[n].persistent_refcount += refcount;
            else
                st->strings[n].nonpersistent_refcount += refcount;
            return n;
        }
        n = st_find_free_entry( st );
        if( n == -1 )
            return -1;
    }

    if( n < 1 )
    {
        ERR("invalid index adding %s (%d)\n", debugstr_a( data ), n );
        return -1;
    }

    /* allocate a new string */
    if( len < 0 )
        len = strlen(data);
    sz = MultiByteToWideChar( st->codepage, 0, data, len, NULL, 0 );
    str = msi_alloc( (sz+1)*sizeof(WCHAR) );
    if( !str )
        return -1;
    MultiByteToWideChar( st->codepage, 0, data, len, str, sz );
    str[sz] = 0;

    set_st_entry( st, n, str, refcount, persistence );

    return n;
}

int msi_addstringW( string_table *st, const WCHAR *data, int len, uint16_t refcount, enum StringPersistence persistence )
{
    unsigned n;
    WCHAR *str;

    if( !data )
        return 0;
    if( !data[0] )
        return 0;

    if( msi_string2idW( st, data, &n ) == ERROR_SUCCESS )
    {
        if (persistence == StringPersistent)
            st->strings[n].persistent_refcount += refcount;
        else
            st->strings[n].nonpersistent_refcount += refcount;
        return n;
    }

    n = st_find_free_entry( st );
    if( n == -1 )
        return -1;

    /* allocate a new string */
    if(len<0)
        len = strlenW(data);
    TRACE("%s, n = %d len = %d\n", debugstr_w(data), n, len );

    str = msi_alloc( (len+1)*sizeof(WCHAR) );
    if( !str )
        return -1;
    memcpy( str, data, len*sizeof(WCHAR) );
    str[len] = 0;

    set_st_entry( st, n, str, refcount, persistence );

    return n;
}

/* find the string identified by an id - return null if there's none */
const WCHAR *msi_string_lookup_id( const string_table *st, unsigned id )
{
    if( id == 0 )
        return szEmpty;

    if( id >= st->maxcount )
        return NULL;

    if( id && !st->strings[id].persistent_refcount && !st->strings[id].nonpersistent_refcount)
        return NULL;

    return st->strings[id].str;
}

/*
 *  msi_id2stringA
 *
 *  [in] st         - pointer to the string table
 *  [in] id         - id of the string to retrieve
 *  [out] buffer    - destination of the UTF8 string
 *  [in/out] sz     - number of bytes available in the buffer on input
 *                    number of bytes used on output
 *
 *  Returned string is not nul terminated.
 */
static unsigned msi_id2stringA( const string_table *st, unsigned id, char *buffer, unsigned *sz )
{
    unsigned len, lenW;
    const WCHAR *str;

    TRACE("Finding string %d of %d\n", id, st->maxcount);

    str = msi_string_lookup_id( st, id );
    if( !str )
        return ERROR_FUNCTION_FAILED;

    lenW = strlenW( str );
    len = WideCharToMultiByte( st->codepage, 0, str, lenW, NULL, 0, NULL, NULL );
    if( *sz < len )
    {
        *sz = len;
        return ERROR_MORE_DATA;
    }
    *sz = WideCharToMultiByte( st->codepage, 0, str, lenW, buffer, *sz, NULL, NULL );
    return ERROR_SUCCESS;
}

/*
 *  msi_string2idW
 *
 *  [in] st         - pointer to the string table
 *  [in] str        - string to find in the string table
 *  [out] id        - id of the string, if found
 */
unsigned msi_string2idW( const string_table *st, const WCHAR *str, unsigned *id )
{
    int i, c, low = 0, high = st->sortcount - 1;

    while (low <= high)
    {
        i = (low + high) / 2;
        c = strcmpW( str, st->strings[st->sorted[i]].str );

        if (c < 0)
            high = i - 1;
        else if (c > 0)
            low = i + 1;
        else
        {
            *id = st->sorted[i];
            return ERROR_SUCCESS;
        }
    }

    return ERROR_INVALID_PARAMETER;
}

static void string_totalsize( const string_table *st, unsigned *datasize, unsigned *poolsize )
{
    unsigned i, len, holesize;

    if( st->strings[0].str || st->strings[0].persistent_refcount || st->strings[0].nonpersistent_refcount)
        ERR("oops. element 0 has a string\n");

    *poolsize = 4;
    *datasize = 0;
    holesize = 0;
    for( i=1; i<st->maxcount; i++ )
    {
        if( !st->strings[i].persistent_refcount )
        {
            TRACE("[%u] nonpersistent = %s\n", i, debugstr_w(st->strings[i].str));
            (*poolsize) += 4;
        }
        else if( st->strings[i].str )
        {
            TRACE("[%u] = %s\n", i, debugstr_w(st->strings[i].str));
            len = WideCharToMultiByte( st->codepage, 0,
                     st->strings[i].str, -1, NULL, 0, NULL, NULL);
            if( len )
                len--;
            (*datasize) += len;
            if (len>0xffff)
                (*poolsize) += 4;
            (*poolsize) += holesize + 4;
            holesize = 0;
        }
        else
            holesize += 4;
    }
    TRACE("data %u pool %u codepage %x\n", *datasize, *poolsize, st->codepage );
}

HRESULT msi_init_string_table( IStorage *stg )
{
    uint16_t zero[2] = { 0, 0 };
    unsigned ret;

    /* create the StringPool stream... add the zero string to it*/
    ret = write_stream_data(stg, szStringPool, zero, sizeof zero, true);
    if (ret != ERROR_SUCCESS)
        return E_FAIL;

    /* create the StringData stream... make it zero length */
    ret = write_stream_data(stg, szStringData, NULL, 0, true);
    if (ret != ERROR_SUCCESS)
        return E_FAIL;

    return S_OK;
}

string_table *msi_load_string_table( IStorage *stg, unsigned *bytes_per_strref )
{
    string_table *st = NULL;
    char *data = NULL;
    uint16_t *pool = NULL;
    unsigned r, datasize = 0, poolsize = 0, codepage;
    unsigned i, count, offset, len, n, refs;

    r = read_stream_data( stg, szStringPool, true, (uint8_t **)&pool, &poolsize );
    if( r != ERROR_SUCCESS)
        goto end;
    r = read_stream_data( stg, szStringData, true, (uint8_t **)&data, &datasize );
    if( r != ERROR_SUCCESS)
        goto end;

    if ( (poolsize > 4) && (pool[1] & 0x8000) )
        *bytes_per_strref = LONG_STR_BYTES;
    else
        *bytes_per_strref = sizeof(uint16_t);

    count = poolsize/4;
    if( poolsize > 4 )
        codepage = pool[0] | ( (pool[1] & ~0x8000) << 16 );
    else
        codepage = CP_ACP;
    st = init_stringtable( count, codepage );
    if (!st)
        goto end;

    offset = 0;
    n = 1;
    i = 1;
    while( i<count )
    {
        /* the string reference count is always the second word */
        refs = pool[i*2+1];

        /* empty entries have two zeros, still have a string id */
        if (pool[i*2] == 0 && refs == 0)
        {
            i++;
            n++;
            continue;
        }

        /*
         * If a string is over 64k, the previous string entry is made null
         * and its the high word of the length is inserted in the null string's
         * reference count field.
         */
        if( pool[i*2] == 0)
        {
            len = (pool[i*2+3] << 16) + pool[i*2+2];
            i += 2;
        }
        else
        {
            len = pool[i*2];
            i += 1;
        }

        if ( (offset + len) > datasize )
        {
            ERR("string table corrupt?\n");
            break;
        }

        r = msi_addstring( st, n, data+offset, len, refs, StringPersistent );
        if( r != n )
            ERR("Failed to add string %d\n", n );
        n++;
        offset += len;
    }

    if ( datasize != offset )
        ERR("string table load failed! (%08x != %08x), please report\n", datasize, offset );

    TRACE("Loaded %d strings\n", count);

end:
    msi_free( pool );
    msi_free( data );

    return st;
}

unsigned msi_save_string_table( const string_table *st, IStorage *storage, unsigned *bytes_per_strref )
{
    unsigned i, datasize = 0, poolsize = 0, sz, used, r, codepage, n;
    unsigned ret = ERROR_FUNCTION_FAILED;
    char *data = NULL;
    uint16_t *pool = NULL;

    TRACE("\n");

    /* construct the new table in memory first */
    string_totalsize( st, &datasize, &poolsize );

    TRACE("%u %u %u\n", st->maxcount, datasize, poolsize );

    pool = msi_alloc( poolsize );
    if( ! pool )
    {
        WARN("Failed to alloc pool %d bytes\n", poolsize );
        goto err;
    }
    data = msi_alloc( datasize );
    if( ! data )
    {
        WARN("Failed to alloc data %d bytes\n", datasize );
        goto err;
    }

    used = 0;
    codepage = st->codepage;
    pool[0] = codepage & 0xffff;
    pool[1] = codepage >> 16;
    if (st->maxcount > 0xffff)
    {
        pool[1] |= 0x8000;
        *bytes_per_strref = LONG_STR_BYTES;
    }
    else
        *bytes_per_strref = sizeof(uint16_t);

    n = 1;
    for( i=1; i<st->maxcount; i++ )
    {
        if( !st->strings[i].persistent_refcount )
        {
            pool[ n*2 ] = 0;
            pool[ n*2 + 1] = 0;
            n++;
            continue;
        }

        sz = datasize - used;
        r = msi_id2stringA( st, i, data+used, &sz );
        if( r != ERROR_SUCCESS )
        {
            ERR("failed to fetch string\n");
            sz = 0;
        }

        if (sz)
            pool[ n*2 + 1 ] = st->strings[i].persistent_refcount;
        else
            pool[ n*2 + 1 ] = 0;
        if (sz < 0x10000)
        {
            pool[ n*2 ] = sz;
            n++;
        }
        else
        {
            pool[ n*2 ] = 0;
            pool[ n*2 + 2 ] = sz&0xffff;
            pool[ n*2 + 3 ] = (sz>>16);
            n += 2;
        }
        used += sz;
        if( used > datasize  )
        {
            ERR("oops overran %d >= %d\n", used, datasize);
            goto err;
        }
    }

    if( used != datasize )
    {
        ERR("oops used %d != datasize %d\n", used, datasize);
        goto err;
    }

    /* write the streams */
    r = write_stream_data( storage, szStringData, data, datasize, true );
    TRACE("Wrote StringData r=%08x\n", r);
    if( r )
        goto err;
    r = write_stream_data( storage, szStringPool, pool, poolsize, true );
    TRACE("Wrote StringPool r=%08x\n", r);
    if( r )
        goto err;

    ret = ERROR_SUCCESS;

err:
    msi_free( data );
    msi_free( pool );

    return ret;
}

unsigned msi_get_string_table_codepage( const string_table *st )
{
    return st->codepage;
}

unsigned msi_set_string_table_codepage( string_table *st, unsigned codepage )
{
    if (validate_codepage( codepage ))
    {
        st->codepage = codepage;
        return ERROR_SUCCESS;
    }
    return ERROR_FUNCTION_FAILED;
}
