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

#include <string.h>

#ifdef USE_QUARTZ
#include <ApplicationServices/ApplicationServices.h>
#elif defined(USE_GDKPIXBUF)
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

gboolean
_cogl_bitmap_can_convert (CoglPixelFormat src, CoglPixelFormat dst)
{
  return FALSE;
}

gboolean
_cogl_bitmap_can_unpremult (CoglPixelFormat format)
{
  return FALSE;
}

gboolean
_cogl_bitmap_convert (const CoglBitmap *bmp,
		      CoglBitmap       *dst_bmp,
		      CoglPixelFormat   dst_format)
{
  /* Now bitmap-fallback is improved there is no need for all this, errm...
   * clutter in here  */
  return FALSE;
}

gboolean
_cogl_bitmap_unpremult (const CoglBitmap *bmp,
			CoglBitmap       *dst_bmp)
{
  return FALSE;
}

/* check if we have transparent, semi-transparent and/or opaque pixels in this
 * image */
void
_cogl_bitmap_check_alpha(CoglBitmap  *bmp,
                        gboolean *has_transparent,
                        gboolean *has_semi,
                        gboolean *has_opaque)
{
  gulong   ctransparent = 0;
  gulong   csemi = 0;
  gulong   copaque = 0;
  gulong   ctotal = 0;
  guint    xpos, ypos;

  if (!bmp)
    {
      if (has_transparent) *has_transparent = TRUE;
      if (has_semi) *has_semi = TRUE;
      if (has_opaque) *has_opaque = TRUE;
      return;
    }

  if ((bmp->format & COGL_AFIRST_BIT) || (bmp->format & COGL_PREMULT_BIT))
    {
      g_warning("%s: Cannot cope with strangely ordered bitmaps", G_STRLOC);
      if (has_transparent) *has_transparent = TRUE;
      if (has_semi) *has_semi = TRUE;
      if (has_opaque) *has_opaque = TRUE;
      return;
    }

  if (!(bmp->format & COGL_A_BIT))
    {
      if (has_transparent) *has_transparent = FALSE;
      if (has_semi) *has_semi = FALSE;
      if (has_opaque) *has_opaque = TRUE;
    }

  if (_cogl_get_format_bpp (bmp->format) != 4)
    {
      g_warning("%s: Expecting only 32 bit bitmaps here", G_STRLOC);
      return;
    }

  for (ypos=0; ypos<bmp->height; ypos++)
   {
     guchar *src = (guchar*)(bmp->data) + ypos * bmp->rowstride + 3;
     for (xpos=0; xpos<bmp->width; xpos++)
       {
         /* If we're doing this, we'd be using 4 bit alpha anyway,
          * so allow ourselves some leeway in what we call transparent
          * and opaque.
          * as far as I know, C standard doesn't say a bool has to be
          * between 0 and 1, so I use ternary to make sure. I *hope*
          * this gets optimised out */
         ctransparent += ((*src) < 16) ? 1 : 0;
         copaque += ((*src) >= 240) ? 1 : 0;
         src += 4;
       }
   }

  /* Now another fudge check... we only say we have something
   * if there are >2% of the pixels set in that way */

  ctotal = bmp->width*bmp->height;
  csemi = ctotal - (ctransparent + copaque);

  if (has_transparent)
    *has_transparent = ctransparent > /*ctotal/50*/0;
  if (has_semi)
    *has_semi = csemi > /*ctotal/50*/0;
  if (has_opaque)
    *has_opaque = copaque > /*ctotal/50*/0;
}

/* Find the best 16 bit format for this bitmap. It can
 * do it by checking pixels */
CoglPixelFormat
_cogl_bitmap_get_16bit_format(CoglBitmap *bmp)
{
  if (bmp->format & COGL_A_BIT)
    {
      /* kinda slow and ugly, but we have multiple formats we could use.
       * we need to look at the image and see what pixel format
       * suits it best, to get the max colour bits */
      gboolean has_transparent, has_semi, has_opaque;
      _cogl_bitmap_check_alpha(bmp,
                               &has_transparent,
                               &has_semi,
                               &has_opaque);
      if (has_semi)
        return COGL_PIXEL_FORMAT_RGBA_4444;
      else if (!has_transparent)
        return COGL_PIXEL_FORMAT_RGB_565;
      else
        return COGL_PIXEL_FORMAT_RGBA_5551;
  } else
    return COGL_PIXEL_FORMAT_RGB_565;
}

#ifdef USE_QUARTZ

/* lacking GdkPixbuf and other useful GError domains, define one of our own */

#define COGL_BITMAP_ERROR cogl_bitmap_error_quark ()

typedef enum {
  COGL_BITMAP_ERROR_FAILED,
  COGL_BITMAP_ERROR_UNKNOWN_TYPE,
  COGL_BITMAP_ERROR_CORRUPT_IMAGE
} CoglBitmapError;

GQuark
cogl_bitmap_error_quark (void)
{
  return g_quark_from_static_string ("cogl-bitmap-error-quark");
}

/* the error does not contain the filename as the caller already has it */
gboolean
_cogl_bitmap_from_file (CoglBitmap  *bmp,
			const gchar *filename,
			GError     **error)
{
  g_assert (bmp != NULL);
  g_assert (filename != NULL);
  g_assert (error == NULL || *error == NULL);

  CFURLRef url = CFURLCreateFromFileSystemRepresentation (NULL, (guchar*)filename, strlen(filename), false);
  CGImageSourceRef image_source = CGImageSourceCreateWithURL (url, NULL);
  int save_errno = errno;
  CFRelease (url);
  if (image_source == NULL)
    {
      /* doesn't exist, not readable, etc. */
      g_set_error (error, COGL_BITMAP_ERROR, COGL_BITMAP_ERROR_FAILED,
                   "%s", g_strerror (save_errno));
      return FALSE;
    }

  /* Unknown images would be cleanly caught as zero width/height below, but try
   * to provide better error message
   */
  CFStringRef type = CGImageSourceGetType (image_source);
  if (type == NULL)
    {
      CFRelease (image_source);
      g_set_error (error, COGL_BITMAP_ERROR, COGL_BITMAP_ERROR_UNKNOWN_TYPE,
                   "Unknown image type");
      return FALSE;
    }
  CFRelease (type);

  CGImageRef image = CGImageSourceCreateImageAtIndex (image_source, 0, NULL);
  CFRelease (image_source);

  size_t width = CGImageGetWidth (image);
  size_t height = CGImageGetHeight (image);
  if (width == 0 || height == 0)
    {
      /* incomplete or corrupt */
      CFRelease (image);
      g_set_error (error, COGL_BITMAP_ERROR, COGL_BITMAP_ERROR_CORRUPT_IMAGE,
                   "Image has zero width or height");
      return FALSE;
    }

  /* allocate buffer big enough to hold pixel data */
  size_t rowstride;
  CGBitmapInfo bitmap_info = CGImageGetBitmapInfo (image);
  if ((bitmap_info & kCGBitmapAlphaInfoMask) == kCGImageAlphaNone)
    {
      bitmap_info = kCGImageAlphaNone;
      rowstride = 3 * width;
    }
  else
    {
      bitmap_info = kCGImageAlphaPremultipliedFirst;
      rowstride = 4 * width;
    }
  guint8 *out_data = g_malloc0 (height * rowstride);

  /* render to buffer */
  CGColorSpaceRef color_space = CGColorSpaceCreateWithName (kCGColorSpaceGenericRGB);
  CGContextRef bitmap_context = CGBitmapContextCreate (out_data,
                                                       width, height, 8,
                                                       rowstride, color_space,
                                                       bitmap_info);
  CGColorSpaceRelease (color_space);

  const CGRect rect = {{0, 0}, {width, height}};
  CGContextDrawImage (bitmap_context, rect, image);

  CGImageRelease (image);
  CGContextRelease (bitmap_context);

  /* store bitmap info */
  bmp->data = out_data;
  bmp->format = bitmap_info == kCGImageAlphaPremultipliedFirst
                ? COGL_PIXEL_FORMAT_ARGB_8888
                : COGL_PIXEL_FORMAT_RGB_888;
  bmp->width = width;
  bmp->height = height;
  bmp->rowstride = rowstride;

  return TRUE;
}

#elif defined(USE_GDKPIXBUF)

gboolean
_cogl_bitmap_from_file (CoglBitmap  *bmp,
			const gchar *filename,
			GError     **error)
{
  GdkPixbuf        *pixbuf;
  gboolean          has_alpha;
  GdkColorspace     color_space;
  CoglPixelFormat   pixel_format;
  gint              width;
  gint              height;
  gint              rowstride;
  gint              bits_per_sample;
  gint              n_channels;
  gint              last_row_size;
  guchar           *pixels;
  guchar           *out_data;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (bmp == NULL) return FALSE;

  /* Load from file using GdkPixbuf */
  pixbuf = gdk_pixbuf_new_from_file (filename, error);
  if (pixbuf == NULL) return FALSE;

  /* Get pixbuf properties */
  has_alpha       = gdk_pixbuf_get_has_alpha (pixbuf);
  color_space     = gdk_pixbuf_get_colorspace (pixbuf);
  width           = gdk_pixbuf_get_width (pixbuf);
  height          = gdk_pixbuf_get_height (pixbuf);
  rowstride       = gdk_pixbuf_get_rowstride (pixbuf);
  bits_per_sample = gdk_pixbuf_get_bits_per_sample (pixbuf);
  n_channels      = gdk_pixbuf_get_n_channels (pixbuf);

  /* The docs say this is the right way */
  last_row_size = width * ((n_channels * bits_per_sample + 7) / 8);

  /* According to current docs this should be true and so
   * the translation to cogl pixel format below valid */
  g_assert (bits_per_sample == 8);

  if (has_alpha)
    g_assert (n_channels == 4);
  else
    g_assert (n_channels == 3);

  /* Translate to cogl pixel format */
  switch (color_space)
    {
    case GDK_COLORSPACE_RGB:
      /* The only format supported by GdkPixbuf so far */
      pixel_format = has_alpha ?
	COGL_PIXEL_FORMAT_RGBA_8888 :
	COGL_PIXEL_FORMAT_RGB_888;
      break;

    default:
      /* Ouch, spec changed! */
      g_object_unref (pixbuf);
      return FALSE;
    }

  /* FIXME: Any way to destroy pixbuf but retain pixel data? */

  pixels   = gdk_pixbuf_get_pixels (pixbuf);
  out_data = (guchar*) g_malloc (height * rowstride);

  /* Copy the data... we need to do special things for the last row */
  memcpy (out_data, pixels, rowstride*(height-1) + last_row_size);

  /* Destroy GdkPixbuf object */
  g_object_unref (pixbuf);

  /* Store bitmap info */
  bmp->data = out_data; /* The stored data the same alignment constraints as a
                         * gdkpixbuf but stores a full rowstride in the last
                         * scanline
                         */
  bmp->format = pixel_format;
  bmp->width = width;
  bmp->height = height;
  bmp->rowstride = rowstride;

  return TRUE;
}

#else

#include "stb_image.c"

gboolean
_cogl_bitmap_from_file (CoglBitmap  *bmp,
			const gchar *filename,
			GError     **error)
{
  gint              stb_pixel_format;
  gint              width;
  gint              height;
  guchar           *pixels;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (bmp == NULL) return FALSE;

  /* Load from file using stb */
  pixels = stbi_load (filename, &width, &height, &stb_pixel_format, STBI_rgb_alpha);
  if (pixels == NULL) return FALSE;

  /* Store bitmap info */
  bmp->data = g_memdup (pixels, height * width * 4);
  bmp->format = COGL_PIXEL_FORMAT_RGBA_8888;
  bmp->width = width;
  bmp->height = height;
  bmp->rowstride = width * 4;

  free (pixels);

  return TRUE;
}
#endif


