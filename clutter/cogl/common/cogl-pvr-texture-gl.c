/*
 * This file is part of Clutter
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Authored By Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "cogl.h"
#include "cogl-internal.h"
#include "cogl-pvr-texture-gl.h"

#if CLUTTER_COGL_HAS_GLES
#include <GLES2/gl2.h>
//#include <GLES2/gl2extimg.h>
#endif

#if CLUTTER_COGL_HAS_GL
#include <GL/gl.h>
#endif

#include <glib/gstdio.h>
/*#include <stdio.h>
#include <stdlib.h>
#include <string.h>*/

/* These are defined in GLES2/gl2ext + gl2extimg, but we want them available
 * so we can compile without the SGX/Imagination libraries */
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG                       0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG                       0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG                      0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG                      0x8C03
#define GL_ETC1_RGB8_OES                                         0x8D64

/*
 * cogl_pvr_texture_load:
 *
 * Loads a '.pvr' texture file into OpenGL and returns the clutter texture.
 * Has a fallback of decompressing if the texture is PVRTC4 and the GPU
 * doesn't support it.
 *
 * Since: 0.8.2-maemo
 */
CoglHandle cogl_pvr_texture_load(const char *filename)
{
  GLuint tex;
  PVR_TEXTURE_HEADER header;
  guchar *texture_data;
  GLuint gl_format = 0;
  FILE *texfile = 0;
  guint read_count;

  /* load file */
  texfile = g_fopen(filename, "rb");
  if (!texfile)
    return 0;

  read_count = fread(&header, 1, sizeof(PVR_TEXTURE_HEADER), texfile);
  if (read_count != sizeof(PVR_TEXTURE_HEADER))
    {
      g_warning("%s: File not large enough for header", __FUNCTION__);
      fclose (texfile);
      return 0;
    }

  /* checks */
  if (((header.dwPVR      ) & 0xFF) != 'P' &&
      ((header.dwPVR >>  8) & 0xFF) != 'V' &&
      ((header.dwPVR >> 16) & 0xFF) != 'R' &&
      ((header.dwPVR >> 24) & 0xFF) != '!')
    {
      g_warning("%s: Invalid PVR magic number 0x%08x", __FUNCTION__,
                header.dwPVR);
      fclose (texfile);
      return 0;
    }

  /* load image */
  texture_data = g_malloc(header.dwDataSize);
  if (!texture_data)
    {
      g_warning("%s: Couldn't allocate texture data", __FUNCTION__);
      fclose (texfile);
      return 0;
    }
  read_count = fread(texture_data, 1, header.dwDataSize, texfile);
  if (read_count != header.dwDataSize)
    {
      g_warning("%s: File not large enough for image data header describes",
                __FUNCTION__);
      fclose (texfile);
      g_free (texture_data);
      return 0;
    }

  fclose(texfile);

  /* work out format */
  if ((header.dwpfFlags & 0xFF) == MGLPT_PVRTC2)
    {
      if (header.dwAlphaBitMask)
        gl_format = GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG;
      else
        gl_format = GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG;
      /* We have no fallback for PVRTC2 */
      if (!cogl_features_available(COGL_FEATURE_TEXTURE_PVRTC))
        {
          g_free(texture_data);
          texture_data = 0;
        }
    }
  else if ((header.dwpfFlags & 0xFF) == MGLPT_PVRTC4)
    {
      if (header.dwAlphaBitMask)
        gl_format = GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG;
      else
        gl_format = GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
      /* If we don't support PVRTC4, decompress it and use that */
      if (!cogl_features_available(COGL_FEATURE_TEXTURE_PVRTC))
        {
          guchar *uncompressed;

          uncompressed = pvr_texture_decompress_pvrtc4(
                texture_data, header.dwWidth, header.dwHeight);
          gl_format = GL_RGBA;

          g_free(texture_data);
          texture_data = uncompressed;
        }
    }
  else if ((header.dwpfFlags & 0xFF) == ETC_RGB_4BPP)
    {
        gl_format = GL_ETC1_RGB8_OES;
    }
  else
    g_warning("%s: Unknown PVR file format 0x%02x", __FUNCTION__,
              header.dwpfFlags & 0xFF);

  /* load into GL */
  GE( glEnable(GL_TEXTURE_2D) );
  GE( glGenTextures(1, &tex) );
  GE( glBindTexture(GL_TEXTURE_2D, tex) );

  if (!texture_data)
    return COGL_INVALID_HANDLE;

  if (gl_format == GL_RGBA)
    {
      CoglHandle tex;
      /* we've had to fall back to decompressing the texture */
      tex =  cogl_texture_new_from_data    (header.dwWidth, header.dwHeight,
                                            0, 0,
                                            COGL_PIXEL_FORMAT_RGBA_8888,
                                            COGL_PIXEL_FORMAT_ANY,
                                            header.dwWidth*4,
                                            texture_data);
      g_free(texture_data);
      return tex;
    }
  else
    {
      GE( glCompressedTexImage2D(GL_TEXTURE_2D, 0, gl_format,
                             header.dwWidth, header.dwHeight, 0,
                             header.dwDataSize, texture_data) );
      g_free(texture_data);
      /* texture format is NOT COGL_PIXEL_FORMAT_RGBA_4444, but we
       * don't have the correct one */
      return cogl_texture_new_from_foreign (
                tex, GL_TEXTURE_2D,
                header.dwWidth, header.dwHeight,
                0, 0,
                COGL_PIXEL_FORMAT_RGBA_4444);
    }
}
