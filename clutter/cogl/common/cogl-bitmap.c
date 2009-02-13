/*
 * Clutter COGL
 *
 * A basic GL/GLES Abstraction/Utility Layer
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *
 * Copyright (C) 2007 OpenedHand
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cogl.h"
#include "cogl-internal.h"
#include "cogl-bitmap.h"
#include "cogl-pvr-texture.h"

#include <string.h>

inline gint
_cogl_get_format_bpp (CoglPixelFormat format)
{
  switch (format & COGL_PIXEL_SIZE_MASK)
    {
  case COGL_PIXEL_FORMAT_8 & COGL_UNORDERED_MASK :
        return 1;
  case COGL_PIXEL_FORMAT_16 & COGL_UNORDERED_MASK :
        return 2;
  case COGL_PIXEL_FORMAT_24 & COGL_UNORDERED_MASK :
        return 3;
  case COGL_PIXEL_FORMAT_32 & COGL_UNORDERED_MASK :
        return 4;
  default:
        g_warning("%s: UNKNOWN PIXEL FORMAT %d", G_STRLOC, format);
    }

  return 0;
}

gboolean
_cogl_bitmap_convert_and_premult (const CoglBitmap *bmp,
				  CoglBitmap       *dst_bmp,
				  CoglPixelFormat   dst_format)
{
  CoglBitmap  tmp_bmp = *bmp;
  CoglBitmap  new_bmp = *bmp;
  gboolean    new_bmp_owner = FALSE;

  /* Is base format different (not considering premult status)? */
  if ((bmp->format & COGL_UNPREMULT_MASK) !=
      (dst_format & COGL_UNPREMULT_MASK))
    {
      /* Try converting using imaging library */
      if (!_cogl_bitmap_convert (&new_bmp, &tmp_bmp, dst_format))
	{
          /*g_warning("%s: Falling back to slow conversion between pixel formats %d and %d",
                    __FUNCTION__, (int)bmp->format, (int)dst_format);*/
	  /* ... or try fallback */
	  if (!_cogl_bitmap_fallback_convert (&new_bmp, &tmp_bmp, dst_format))
            {
	      return FALSE;
            }
	}

      /* Update bitmap with new data */
      new_bmp = tmp_bmp;
      new_bmp_owner = TRUE;
    }

  /* Do we need to unpremultiply */
  if ((bmp->format & COGL_PREMULT_BIT) == 0 &&
      (dst_format & COGL_PREMULT_BIT) > 0)
    {
      /* Try unpremultiplying using imaging library */
      if (!_cogl_bitmap_unpremult (&new_bmp, &tmp_bmp))
	{
	  /* ... or try fallback */
	  if (!_cogl_bitmap_fallback_unpremult (&new_bmp, &tmp_bmp))
	    {
	      if (new_bmp_owner)
		g_free (new_bmp.data);

	      return FALSE;
	    }
	}

      /* Update bitmap with new data */
      if (new_bmp_owner)
	g_free (new_bmp.data);

      new_bmp = tmp_bmp;
      new_bmp_owner = TRUE;
    }

  /* Do we need to premultiply */
  if ((bmp->format & COGL_PREMULT_BIT) > 0 &&
      (dst_format & COGL_PREMULT_BIT) == 0)
    {
      /* FIXME: implement premultiplication */
      if (new_bmp_owner)
	g_free (new_bmp.data);

      return FALSE;
    }

  /* Output new bitmap info */
  *dst_bmp = new_bmp;

  return TRUE;
}

void
_cogl_bitmap_copy_subregion (CoglBitmap *src,
			     CoglBitmap *dst,
			     gint        src_x,
			     gint        src_y,
			     gint        dst_x,
			     gint        dst_y,
			     gint        width,
			     gint        height)
{
  guchar *srcdata;
  guchar *dstdata;
  gint    bpp;
  gint    line;

  /* Intended only for fast copies when format is equal! */
  g_assert (src->format == dst->format);
  bpp = _cogl_get_format_bpp (src->format);

  srcdata = src->data + src_y * src->rowstride + src_x * bpp;
  dstdata = dst->data + dst_y * dst->rowstride + dst_x * bpp;

  for (line=0; line<height; ++line)
    {
      memcpy (dstdata, srcdata, width * bpp);
      srcdata += src->rowstride;
      dstdata += dst->rowstride;
    }
}

gboolean        cogl_bitmap_save_pvrtc4       (const gchar *filename,
                                               const guchar *data,
                                               gint width,
                                               gint height,
                                               gboolean has_alpha)
{
  guchar *compressed = 0;
  const guchar *uncompressed = data;
  guchar *rgba = 0;
  guint compressed_size = 0;

  if (!has_alpha)
    {
      gint i,s;
      guchar *uc;
      /* do conversion to RGBA8888... */
      s = width*height;
      rgba = g_malloc(4*s);
      uncompressed = uc = rgba;
      for (i=0;i<s;i++)
        {
          uc[0] = data[3*i  ];
          uc[1] = data[3*i+1];
          uc[2] = data[3*i+2];
          uc[3] = 255;
          uc += 4;
        }
    }

  compressed = cogl_pvr_texture_compress_pvrtc4(
                        uncompressed, width, height,
                        &compressed_size);
  /* free data if we created it above */
  if (rgba)
    g_free(rgba);

  if (!compressed)
    return FALSE;

  if (!cogl_pvr_texture_save_pvrtc4( filename, compressed, compressed_size,
                                     width, height))
    return FALSE;

  g_free (compressed);
  return TRUE;
}
