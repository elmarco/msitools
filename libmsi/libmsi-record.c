/*
 * Implementation of the Microsoft Installer (msi.dll)
 *
 * Copyright 2002-2004 Mike McCormack for CodeWeavers
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

#include <stdarg.h>

#include "libmsi-record.h"

#include "debug.h"
#include "libmsi.h"
#include "msipriv.h"
#include "query.h"

enum
{
    PROP_0,

    PROP_COUNT
};

G_DEFINE_TYPE (LibmsiRecord, libmsi_record, G_TYPE_OBJECT);

#define GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE((o), LIBMSI_TYPE_RECORD, LibmsiRecordPrivate))

#define LIBMSI_FIELD_TYPE_NULL   0
#define LIBMSI_FIELD_TYPE_INT    1
#define LIBMSI_FIELD_TYPE_STR   3
#define LIBMSI_FIELD_TYPE_STREAM 4

static void
libmsi_record_init (LibmsiRecord *self)
{
    LibmsiRecordPrivate *p = GET_PRIVATE (self);

    self->priv = p;
}

static void
_libmsi_free_field (LibmsiField *field )
{
    switch (field->type) {
    case LIBMSI_FIELD_TYPE_NULL:
    case LIBMSI_FIELD_TYPE_INT:
        break;
    case LIBMSI_FIELD_TYPE_STR:
        g_free (field->u.szVal);
        field->u.szVal = NULL;
        break;
    case LIBMSI_FIELD_TYPE_STREAM:
        if (field->u.stream) {
            g_object_unref (G_OBJECT (field->u.stream));
            field->u.stream = NULL;
        }
        break;
    default:
        ERR ("Invalid field type %d\n", field->type);
    }
}

static void
libmsi_record_finalize (GObject *object)
{
    LibmsiRecordPrivate *p = LIBMSI_RECORD (object)->priv;
    unsigned i;

    for (i = 0; i <= p->count; i++ )
        _libmsi_free_field (&p->fields[i]);

    g_free (p->fields);

    G_OBJECT_CLASS (libmsi_record_parent_class)->finalize (object);
}

static void
libmsi_record_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    g_return_if_fail (LIBMSI_IS_RECORD (object));
    LibmsiRecordPrivate *p = LIBMSI_RECORD (object)->priv;

    switch (prop_id) {
    case PROP_COUNT:
        p->count = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
libmsi_record_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    g_return_if_fail (LIBMSI_IS_RECORD (object));
    LibmsiRecordPrivate *p = LIBMSI_RECORD (object)->priv;

    switch (prop_id) {
    case PROP_COUNT:
        g_value_set_uint (value, p->count);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
libmsi_record_constructed (GObject *object)
{
    LibmsiRecord *self = LIBMSI_RECORD (object);
    LibmsiRecordPrivate *p = self->priv;

    // FIXME: +1 could be removed if accessing with idx-1
    p->fields = g_new0 (LibmsiField, p->count + 1);

    G_OBJECT_CLASS (libmsi_record_parent_class)->constructed (object);
}

static void
libmsi_record_class_init (LibmsiRecordClass *klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = libmsi_record_finalize;
    object_class->set_property = libmsi_record_set_property;
    object_class->get_property = libmsi_record_get_property;
    object_class->constructed = libmsi_record_constructed;

    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COUNT,
        g_param_spec_uint ("count", "count", "count", 0, 65535, 0,
                           G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS));

    g_type_class_add_private (klass, sizeof(LibmsiRecordPrivate));
}

unsigned libmsi_record_get_field_count( const LibmsiRecord *rec )
{
    if( !rec )
        return -1;

    return rec->priv->count;
}

static bool expr_int_from_string( const char *str, int *out )
{
    int x = 0;
    const char *p = str;

    if( *p == '-' ) /* skip the minus sign */
        p++;
    while ( *p )
    {
        if( (*p < '0') || (*p > '9') )
            return false;
        x *= 10;
        x += (*p - '0');
        p++;
    }

    if( str[0] == '-' ) /* check if it's negative */
        x = -x;
    *out = x; 

    return true;
}

unsigned _libmsi_record_copy_field( LibmsiRecord *in_rec, unsigned in_n,
                          LibmsiRecord *out_rec, unsigned out_n )
{
    unsigned r = LIBMSI_RESULT_SUCCESS;

    if ( in_n > in_rec->priv->count || out_n > out_rec->priv->count )
        r = LIBMSI_RESULT_FUNCTION_FAILED;
    else if ( in_rec != out_rec || in_n != out_n )
    {
        char *str;
        LibmsiField *in, *out;

        in = &in_rec->priv->fields[in_n];
        out = &out_rec->priv->fields[out_n];

        switch ( in->type )
        {
        case LIBMSI_FIELD_TYPE_NULL:
            break;
        case LIBMSI_FIELD_TYPE_INT:
            out->u.iVal = in->u.iVal;
            break;
        case LIBMSI_FIELD_TYPE_STR:
            str = strdup( in->u.szVal );
            if ( !str )
                r = LIBMSI_RESULT_OUTOFMEMORY;
            else
                out->u.szVal = str;
            break;
        case LIBMSI_FIELD_TYPE_STREAM:
            g_object_ref(G_OBJECT(in->u.stream));
            out->u.stream = in->u.stream;
            break;
        default:
            ERR("invalid field type %d\n", in->type);
        }
        if (r == LIBMSI_RESULT_SUCCESS)
            out->type = in->type;
    }

    return r;
}

int libmsi_record_get_int( const LibmsiRecord *rec, unsigned field)
{
    int ret = 0;

    TRACE("%p %d\n", rec, field);

    if( !rec )
        return LIBMSI_NULL_INT;

    if( field > rec->priv->count )
        return LIBMSI_NULL_INT;

    switch( rec->priv->fields[field].type )
    {
    case LIBMSI_FIELD_TYPE_INT:
        return rec->priv->fields[field].u.iVal;
    case LIBMSI_FIELD_TYPE_STR:
        if( expr_int_from_string( rec->priv->fields[field].u.szVal, &ret ) )
            return ret;
        return LIBMSI_NULL_INT;
    default:
        break;
    }

    return LIBMSI_NULL_INT;
}

LibmsiResult libmsi_record_clear( LibmsiRecord *rec )
{
    unsigned i;

    TRACE("%d\n", rec );

    if( !rec )
        return LIBMSI_RESULT_INVALID_HANDLE;

    g_object_ref(rec);
    for( i=0; i<=rec->priv->count; i++)
    {
        _libmsi_free_field( &rec->priv->fields[i] );
        rec->priv->fields[i].type = LIBMSI_FIELD_TYPE_NULL;
        rec->priv->fields[i].u.iVal = 0;
    }
    g_object_unref(rec);

    return LIBMSI_RESULT_SUCCESS;
}

LibmsiResult libmsi_record_set_int( LibmsiRecord *rec, unsigned field, int iVal )
{
    TRACE("%p %u %d\n", rec, field, iVal);

    if( !rec )
        return LIBMSI_RESULT_INVALID_HANDLE;

    if( field > rec->priv->count )
        return LIBMSI_RESULT_INVALID_PARAMETER;

    _libmsi_free_field( &rec->priv->fields[field] );
    rec->priv->fields[field].type = LIBMSI_FIELD_TYPE_INT;
    rec->priv->fields[field].u.iVal = iVal;

    return LIBMSI_RESULT_SUCCESS;
}

gboolean libmsi_record_is_null( const LibmsiRecord *rec, unsigned field )
{
    bool r = true;

    TRACE("%p %d\n", rec, field );

    if( !rec )
        return 0;

    r = ( field > rec->priv->count ) ||
        ( rec->priv->fields[field].type == LIBMSI_FIELD_TYPE_NULL );

    return r;
}

gchar* libmsi_record_get_string(const LibmsiRecord *self, unsigned field)
{
    g_return_val_if_fail (LIBMSI_IS_RECORD (self), NULL);

    TRACE ("%p %d\n", self, field);

    if (field > self->priv->count)
        return g_strdup (""); // FIXME: really?

    switch (self->priv->fields[field].type) {
    case LIBMSI_FIELD_TYPE_INT:
        return g_strdup_printf ("%d", self->priv->fields[field].u.iVal);
    case LIBMSI_FIELD_TYPE_STR:
        return g_strdup (self->priv->fields[field].u.szVal);
    case LIBMSI_FIELD_TYPE_NULL:
        return g_strdup ("");
    }

    return NULL;
}

const char *_libmsi_record_get_string_raw( const LibmsiRecord *rec, unsigned field )
{
    if( field > rec->priv->count )
        return NULL;

    if( rec->priv->fields[field].type != LIBMSI_FIELD_TYPE_STR )
        return NULL;

    return rec->priv->fields[field].u.szVal;
}

unsigned _libmsi_record_get_string(const LibmsiRecord *rec, unsigned field,
               char *szValue, unsigned *pcchValue)
{
    unsigned len=0, ret;
    char buffer[16];
    static const char szFormat[] = "%d";

    TRACE("%p %d %p %p\n", rec, field, szValue, pcchValue);

    if( field > rec->priv->count )
    {
        if ( szValue && *pcchValue > 0 )
            szValue[0] = 0;

        *pcchValue = 0;
        return LIBMSI_RESULT_SUCCESS;
    }

    ret = LIBMSI_RESULT_SUCCESS;
    switch( rec->priv->fields[field].type )
    {
    case LIBMSI_FIELD_TYPE_INT:
        sprintf(buffer, szFormat, rec->priv->fields[field].u.iVal);
        len = strlen( buffer );
        if (szValue)
            strcpyn(szValue, buffer, *pcchValue);
        break;
    case LIBMSI_FIELD_TYPE_STR:
        len = strlen( rec->priv->fields[field].u.szVal );
        if (szValue)
            strcpyn(szValue, rec->priv->fields[field].u.szVal, *pcchValue);
        break;
    case LIBMSI_FIELD_TYPE_NULL:
        if( szValue && *pcchValue > 0 )
            szValue[0] = 0;
        break;
    default:
        break;
    }

    if( szValue && *pcchValue <= len )
        ret = LIBMSI_RESULT_MORE_DATA;
    *pcchValue = len;

    return ret;
}

LibmsiResult libmsi_record_set_string( LibmsiRecord *rec, unsigned field, const char *szValue )
{
    char *str;

    TRACE("%d %d %s\n", rec, field, debugstr_a(szValue));

    if( !rec )
        return LIBMSI_RESULT_INVALID_HANDLE;

    if( field > rec->priv->count )
        return LIBMSI_RESULT_INVALID_FIELD;

    _libmsi_free_field( &rec->priv->fields[field] );

    if( szValue && szValue[0] )
    {
        str = strdup( szValue );
        rec->priv->fields[field].type = LIBMSI_FIELD_TYPE_STR;
        rec->priv->fields[field].u.szVal = str;
    }
    else
    {
        rec->priv->fields[field].type = LIBMSI_FIELD_TYPE_NULL;
        rec->priv->fields[field].u.szVal = NULL;
    }

    return 0;
}

/* read the data in a file into a memory-backed GsfInput */
static unsigned _libmsi_addstream_from_file(const char *szFile, GsfInput **pstm)
{
    GsfInput *stm;
    char *data;
    off_t sz;

    stm = gsf_input_stdio_new(szFile, NULL);
    if (!stm)
    {
        WARN("open file failed for %s\n", debugstr_a(szFile));
        return LIBMSI_RESULT_OPEN_FAILED;
    }

    sz = gsf_input_size(stm);
    if (sz == 0)
    {
        data = g_malloc(1);
    }
    else
    {
        data = g_try_malloc(sz);
        if (!data)
            return LIBMSI_RESULT_NOT_ENOUGH_MEMORY;

        if (!gsf_input_read(stm, sz, data))
        {
            g_object_unref(G_OBJECT(stm));
            return LIBMSI_RESULT_FUNCTION_FAILED;
        }
    }

    g_object_unref(G_OBJECT(stm));
    *pstm = gsf_input_memory_new(data, sz, true);

    TRACE("read %s, %d bytes into GsfInput %p\n", debugstr_a(szFile), sz, *pstm);

    return LIBMSI_RESULT_SUCCESS;
}

unsigned _libmsi_record_load_stream(LibmsiRecord *rec, unsigned field, GsfInput *stream)
{
    if ( (field == 0) || (field > rec->priv->count) )
        return LIBMSI_RESULT_INVALID_PARAMETER;

    _libmsi_free_field( &rec->priv->fields[field] );
    rec->priv->fields[field].type = LIBMSI_FIELD_TYPE_STREAM;
    rec->priv->fields[field].u.stream = stream;

    return LIBMSI_RESULT_SUCCESS;
}

unsigned _libmsi_record_load_stream_from_file(LibmsiRecord *rec, unsigned field, const char *szFilename)
{
    GsfInput *stm;
    unsigned r;

    if( (field == 0) || (field > rec->priv->count) )
        return LIBMSI_RESULT_INVALID_PARAMETER;

    /* no filename means we should seek back to the start of the stream */
    if( !szFilename )
    {
        if( rec->priv->fields[field].type != LIBMSI_FIELD_TYPE_STREAM )
            return LIBMSI_RESULT_INVALID_FIELD;

        stm = rec->priv->fields[field].u.stream;
        if( !stm )
            return LIBMSI_RESULT_INVALID_FIELD;

        gsf_input_seek( stm, 0, G_SEEK_SET );
    }
    else
    {
        /* read the file into a stream and save the stream in the record */
        r = _libmsi_addstream_from_file(szFilename, &stm);
        if( r != LIBMSI_RESULT_SUCCESS )
            return r;

        /* if all's good, store it in the record */
        _libmsi_record_load_stream(rec, field, stm);
    }

    return LIBMSI_RESULT_SUCCESS;
}

LibmsiResult libmsi_record_load_stream(LibmsiRecord *rec, unsigned field, const char *szFilename)
{
    unsigned ret;

    TRACE("%d %d %s\n", rec, field, debugstr_a(szFilename));

    if( !rec )
        return LIBMSI_RESULT_INVALID_HANDLE;

    g_object_ref(rec);
    ret = _libmsi_record_load_stream_from_file( rec, field, szFilename );
    g_object_unref(rec);

    return ret;
}

unsigned _libmsi_record_save_stream(const LibmsiRecord *rec, unsigned field, char *buf, unsigned *sz)
{
    uint64_t left;
    GsfInput *stm;

    TRACE("%p %d %p %p\n", rec, field, buf, sz);

    if( !sz )
        return LIBMSI_RESULT_INVALID_PARAMETER;

    if( field > rec->priv->count)
        return LIBMSI_RESULT_INVALID_PARAMETER;

    if ( rec->priv->fields[field].type == LIBMSI_FIELD_TYPE_NULL )
    {
        *sz = 0;
        return LIBMSI_RESULT_INVALID_DATA;
    }

    if( rec->priv->fields[field].type != LIBMSI_FIELD_TYPE_STREAM )
        return LIBMSI_RESULT_INVALID_DATATYPE;

    stm = rec->priv->fields[field].u.stream;
    if( !stm )
        return LIBMSI_RESULT_INVALID_PARAMETER;

    left = gsf_input_size(stm) - gsf_input_tell(stm);

    /* if there's no buffer pointer, calculate the length to the end */
    if( !buf )
    {
        *sz = left;

        return LIBMSI_RESULT_SUCCESS;
    }

    /* read the data */
    if (*sz > left)
        *sz = left;

    if (*sz > 0 && !gsf_input_read( stm, *sz, buf ))
    {
        *sz = 0;
        return LIBMSI_RESULT_FUNCTION_FAILED;
    }

    return LIBMSI_RESULT_SUCCESS;
}

LibmsiResult libmsi_record_save_stream(LibmsiRecord *rec, unsigned field, char *buf, unsigned *sz)
{
    unsigned ret;

    TRACE("%d %d %p %p\n", rec, field, buf, sz);

    if( !rec )
        return LIBMSI_RESULT_INVALID_HANDLE;

    g_object_ref(rec);
    ret = _libmsi_record_save_stream( rec, field, buf, sz );
    g_object_unref(rec);

    return ret;
}

unsigned _libmsi_record_set_gsf_input( LibmsiRecord *rec, unsigned field, GsfInput *stm )
{
    TRACE("%p %d %p\n", rec, field, stm);

    if( field > rec->priv->count )
        return LIBMSI_RESULT_INVALID_FIELD;

    _libmsi_free_field( &rec->priv->fields[field] );

    rec->priv->fields[field].type = LIBMSI_FIELD_TYPE_STREAM;
    rec->priv->fields[field].u.stream = stm;
    g_object_ref(G_OBJECT(stm));

    return LIBMSI_RESULT_SUCCESS;
}

unsigned _libmsi_record_get_gsf_input( const LibmsiRecord *rec, unsigned field, GsfInput **pstm)
{
    TRACE("%p %d %p\n", rec, field, pstm);

    if( field > rec->priv->count )
        return LIBMSI_RESULT_INVALID_FIELD;

    if( rec->priv->fields[field].type != LIBMSI_FIELD_TYPE_STREAM )
        return LIBMSI_RESULT_INVALID_FIELD;

    *pstm = rec->priv->fields[field].u.stream;
    g_object_ref(G_OBJECT(*pstm));

    return LIBMSI_RESULT_SUCCESS;
}

LibmsiRecord *_libmsi_record_clone(LibmsiRecord *rec)
{
    LibmsiRecord *clone;
    unsigned r, i, count;

    count = libmsi_record_get_field_count(rec);
    clone = libmsi_record_new(count);
    if (!clone)
        return NULL;

    for (i = 0; i <= count; i++)
    {
        if (rec->priv->fields[i].type == LIBMSI_FIELD_TYPE_STREAM)
        {
            GsfInput *stm = gsf_input_dup(rec->priv->fields[i].u.stream, NULL);
            if (!stm)
            {
                g_object_unref(clone);
                return NULL;
            }
	    clone->priv->fields[i].u.stream = stm;
            clone->priv->fields[i].type = LIBMSI_FIELD_TYPE_STREAM;
        }
        else
        {
            r = _libmsi_record_copy_field(rec, i, clone, i);
            if (r != LIBMSI_RESULT_SUCCESS)
            {
                g_object_unref(clone);
                return NULL;
            }
        }
    }

    return clone;
}

bool _libmsi_record_compare_fields(const LibmsiRecord *a, const LibmsiRecord *b, unsigned field)
{
    if (a->priv->fields[field].type != b->priv->fields[field].type)
        return false;

    switch (a->priv->fields[field].type)
    {
        case LIBMSI_FIELD_TYPE_NULL:
            break;

        case LIBMSI_FIELD_TYPE_INT:
            if (a->priv->fields[field].u.iVal != b->priv->fields[field].u.iVal)
                return false;
            break;

        case LIBMSI_FIELD_TYPE_STR:
            if (strcmp(a->priv->fields[field].u.szVal, b->priv->fields[field].u.szVal))
                return false;
            break;

        case LIBMSI_FIELD_TYPE_STREAM:
        default:
            return false;
    }
    return true;
}


bool _libmsi_record_compare(const LibmsiRecord *a, const LibmsiRecord *b)
{
    unsigned i;

    if (a->priv->count != b->priv->count)
        return false;

    for (i = 0; i <= a->priv->count; i++)
    {
        if (!_libmsi_record_compare_fields( a, b, i ))
            return false;
    }

    return true;
}

char *msi_dup_record_field( LibmsiRecord *rec, int field )
{
    unsigned sz = 0;
    char *str;
    unsigned r;

    if (libmsi_record_is_null( rec, field )) return NULL;

    r = _libmsi_record_get_string( rec, field, NULL, &sz );
    if (r != LIBMSI_RESULT_SUCCESS)
        return NULL;

    sz++;
    str = msi_alloc( sz * sizeof(char) );
    if (!str) return NULL;
    str[0] = 0;
    r = _libmsi_record_get_string( rec, field, str, &sz );
    if (r != LIBMSI_RESULT_SUCCESS)
    {
        ERR("failed to get string!\n");
        msi_free( str );
        return NULL;
    }
    return str;
}

LibmsiRecord *
libmsi_record_new (guint count)
{
    g_return_val_if_fail (count < 65535, NULL);

    return g_object_new (LIBMSI_TYPE_RECORD,
                         "count", count,
                         NULL);
}
