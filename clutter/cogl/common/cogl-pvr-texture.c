#include "cogl.h"
#include "cogl-internal.h"
#include "cogl-pvr-texture.h"
#include "cogl-util.h"

#if CLUTTER_COGL_HAS_GLES
#include <GLES2/gl2.h>
//#include <GLES2/gl2extimg.h>
#endif

#if CLUTTER_COGL_HAS_GL
#include <GL/gl.h>
#endif

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These are defined in GLES2/gl2ext + gl2extimg, but we want them available
 * so we can compile with OpenGL nicely */
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG                       0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG                       0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG                      0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG                      0x8C03
#define GL_ETC1_RGB8_OES                                         0x8D64

typedef struct PVR_TEXTURE_HEADER_TAG {
    unsigned int dwHeaderSize;     /* size of the structure */
    unsigned int dwHeight;         /* height of surface to be created */
    unsigned int dwWidth;          /* width of input surface */
    unsigned int dwMipMapCount;    /* number of MIP-map levels requested */
    unsigned int dwpfFlags;        /* pixel format flags */
    unsigned int dwDataSize;       /* Size of the compress data */
    unsigned int dwBitCount;       /* number of bits per pixel */
    unsigned int dwRBitMask;       /* mask for red bit */
    unsigned int dwGBitMask;       /* mask for green bits */
    unsigned int dwBBitMask;       /* mask for blue bits */
    unsigned int dwAlphaBitMask;   /* mask for alpha channel */
    unsigned int dwPVR;            /* should be 'P' 'V' 'R' '!' */
    unsigned int dwNumSurfs;       /* number of slices for volume textures or skyboxes */
} PVR_TEXTURE_HEADER;

#define MGLPT_PVRTC2 0x18
#define MGLPT_PVRTC4 0x19             
#define ETC_RGB_4BPP 0x36

#define PVR_FLAG_TWIDDLED 0x00000200
#define PVR_FLAG_ALPHA    0x00008000

/*
 * cogl_pvr_texture_save_pvrtc4:
 *
 * saves an already compressed (with cogl_pvr_texture_compress)
 * data slice to a file. Returns TRUE on success
 *
 * Since: 0.8.2-maemo
 */ 
gboolean cogl_pvr_texture_save_pvrtc4(
                        const gchar *filename, 
                        const guchar *data, 
                        guint data_size, 
                        gint width, gint height)
{
    FILE *texfile;
    PVR_TEXTURE_HEADER head;
    head.dwHeaderSize = sizeof(PVR_TEXTURE_HEADER);     /* size of the structure */
    head.dwHeight = height;         /* height of surface to be created */
    head.dwWidth = width;          /* width of input surface */
    head.dwMipMapCount = 0;    /* number of MIP-map levels requested */
    head.dwpfFlags = MGLPT_PVRTC4 | PVR_FLAG_TWIDDLED | PVR_FLAG_ALPHA;        /* pixel format flags */
    head.dwDataSize = data_size;       /* Size of the compress data */
    head.dwBitCount = 4;       /* number of bits per pixel */
    head.dwRBitMask = 0;       /* mask for red bit */
    head.dwGBitMask = 0;       /* mask for green bits */
    head.dwBBitMask = 0;       /* mask for blue bits */
    head.dwAlphaBitMask = 1;   /* mask for alpha channel */
    head.dwPVR = 'P' | 'V'<<8 | 'R'<<16 | '!'<<24; /* should be 'P' 'V' 'R' '!' */
    head.dwNumSurfs = 1;       /* number of slices for volume textures or skyboxes */
       
    /* load file */
    texfile = g_fopen(filename, "wb");
    if (!texfile) 
      return FALSE;
    
    fwrite(&head, 1, sizeof(PVR_TEXTURE_HEADER), texfile);
    fwrite(data, 1, data_size, texfile);
    fclose(texfile);
    
    return TRUE;
}

#define SETMIN(result, col) { \
        if ((result).red   > (col).red)   (result).red   = (col).red; \
        if ((result).green > (col).green) (result).green = (col).green; \
        if ((result).blue  > (col).blue)  (result).blue  = (col).blue; \
        if ((result).alpha > (col).alpha) (result).alpha = (col).alpha; \
}
#define SETMAX(result, col) { \
        if ((result).red   < (col).red)   (result).red   = (col).red; \
        if ((result).green < (col).green) (result).green = (col).green; \
        if ((result).blue  < (col).blue)  (result).blue  = (col).blue; \
        if ((result).alpha < (col).alpha) (result).alpha = (col).alpha; \
}

inline guchar find_best( 
                ClutterColor pixel_col,
                ClutterColor *low,
                ClutterColor *high,
                guint block_stride,
                guint x_interp,
                guint y_interp)
{  
  ClutterColor tmpa, tmpb;
  guchar amtx, amty;
  ClutterColor cl, ch, clm, chm; 
  guint diff[4];
  
  /* interpolate our colours spatially */
  
  amtx = x_interp * 64;
  amty = y_interp * 64;  
  
  clutter_color_interp(&low[0], &low[1], amtx, &tmpa);
  clutter_color_interp(&low[block_stride], &low[block_stride+1], amtx, &tmpb);
  clutter_color_interp(&tmpa, &tmpb, amty, &cl);
  
  clutter_color_interp(&high[0], &high[1], amtx, &tmpa);
  clutter_color_interp(&high[block_stride], &high[block_stride+1], amtx, &tmpb);
  clutter_color_interp(&tmpa, &tmpb, amty, &ch);
  
  /* interpolate for the mid-colours */
  clutter_color_interp(&cl, &ch, 96, &clm); /* 3/8 */
  clutter_color_interp(&cl, &ch, 160, &chm); /* 5/8 */
  
  /* work out differences */
  diff[0] = clutter_color_diff(&pixel_col, &cl);
  diff[1] = clutter_color_diff(&pixel_col, &clm);
  diff[2] = clutter_color_diff(&pixel_col, &chm);
  diff[3] = clutter_color_diff(&pixel_col, &ch);
  
  /* work out which one is smaller */
  if (diff[0] < diff[1] && diff[0] < diff[2] && diff[0] < diff[3])
    return 0;
  if (diff[1] < diff[2] && diff[1] < diff[3])
    return 1;
  if (diff[2] < diff[3])
    return 2;
  return 3;
}

inline guint clutter_color_to_pvr_color( ClutterColor *col )
{
  /* 16 bit colour, if top bit is 1 it's 555, otherwise
   * it's 3444 */
  if (col->alpha >= 224)
    {
      /* We're opaqueish */ 
      return 0x8000 |
             ((col->red & 0xF8) << 7) |
             ((col->green & 0xF8) << 2) |
             (col->blue >> 3);
    }
  else
    {
      return ((col->alpha & 0xE0) << 7) |
             ((col->red & 0xF0) << 4) |
             (col->green & 0xF0) |
             (col->blue >> 4);
    }
}

inline ClutterColor clutter_pvr_color_to_color( guint32 col )
{
  ClutterColor result;
  /* If top bit is 1, this is full alpha */
  if (col & 0x8000)
    {
      result.alpha = 255;
      result.red = (col>>7) & 0xF8;
      result.green = (col>>2) & 0xF8;
      result.blue = (col<<3) & 0xF8;
    }
  else
    {
      result.alpha = (col>>7) & 0xE0;
      result.red = (col>>4) & 0xF0;
      result.green = col & 0xF0;
      result.blue = col<<4;
    }
  return result;
}

/* calculate the masks needed to access the morton-ordered image.
 * Values must be a power of 2 */
static void _calculate_access_masks( gint width, gint height,
      guint32 *morton_mask,
      guint32 *xshift, guint32 *xmask,
      guint32 *yshift, guint32 *ymask)
{
  *xshift = 0;
  *yshift = 0;
  *xmask = 0;
  *ymask = 0;
  *morton_mask = 0xFFFFFFFF;
  if (width == height)
    {      
      return;
    }
  if (width > height)
    {     
      *morton_mask = (height*height)-1;
      *xshift = cogl_util_log_2(height);
      *xmask = 0xFFFFFFFF & ~*morton_mask;
    }
  else
    { // width < height
      *morton_mask = (width*width)-1;
      *yshift = cogl_util_log_2(width);
      *ymask = 0xFFFFFFFF & ~*morton_mask;
    }
}

/**
 * cogl_pvr_texture_compress_pvrtc4:
 *
 * Takes an RGBA8888 bitmap and returns the data (and size) created
 * after it has been compressed in the PVRTC4 format.
 *
 * Since: 0.8.2-maemo
 */
guchar *cogl_pvr_texture_compress_pvrtc4(
                const guchar *uncompressed_data, 
                gint width, 
                gint height, 
                guint *compressed_size)
{
  guchar *compressed_data = 0;
  guint width_block, height_block, block_stride;
  ClutterColor *col_low, *col_high;
  gint x,y,z;
  guint32 *out_data;
  guint32 morton_mask, xshift, xmask, yshift, ymask;
  
  g_return_val_if_fail(compressed_size!=0, 0);
  /* must be a multiple of 4 + Power of 2 in each direction */
  if ((width&3) || (height&3) || 
      !cogl_util_is_power_2(width) ||
      !cogl_util_is_power_2(height))
    return 0;
  
  width_block = width / 4;
  block_stride = width_block+2;
  height_block = height / 4;  
  _calculate_access_masks(width_block, height_block,
      &morton_mask, &xshift, &xmask, &yshift, &ymask);
  /* 4 bits per pixel, or 64 bits per block*/
  *compressed_size = width_block*height_block*sizeof(guint32)*2; 
  compressed_data = g_malloc(*compressed_size);
  /* but we make our block colour list one bigger all the way around
   * and copy the colours so we don't need to do bounds checking */
  col_low = g_malloc(sizeof(ClutterColor)*block_stride*(height_block+2));
  col_high = g_malloc(sizeof(ClutterColor)*block_stride*(height_block+2));
  
  /* work out maximum and minimum colour values for each block */
  for (y=0;y<height_block;y++)
    {
      guint block_offs = (y+1)*block_stride;
      for (x=0;x<width_block;x++)
        {
          ClutterColor clow, chigh;
          ClutterColor *block;
        
          block = (ClutterColor*)&uncompressed_data[(x + y*width) * 16];
          clow = block[0];
          chigh = block[0];
          SETMIN(clow, block[1]);
          SETMAX(chigh, block[1]);
          SETMIN(clow, block[2]);
          SETMAX(chigh, block[2]);
          SETMIN(clow, block[3]);
          SETMAX(chigh, block[3]);
          for (z=1;z<4;z++)
            {
              ClutterColor *blockline = &block[width*z];
              SETMIN(clow, blockline[0]);
              SETMAX(chigh, blockline[0]);
              SETMIN(clow, blockline[1]);            
              SETMAX(chigh, blockline[1]);
              SETMIN(clow, blockline[2]);
              SETMAX(chigh, blockline[2]);
              SETMIN(clow, blockline[3]);
              SETMAX(chigh, blockline[3]);
            }
          col_low[1+x+block_offs] = clow;
          col_high[1+x+block_offs] = chigh;
        }
      /* copy beginning and end */
      col_low[block_offs] = col_low[block_offs+1];
      col_low[block_offs+width_block+1] = col_low[block_offs+width_block];
      col_high[block_offs] = col_high[block_offs+1];
      col_high[block_offs+width_block+1] = col_high[block_offs+width_block];
    }
  /* copy top and bottom of our block so we get repeats */
  memcpy((void*)&col_low[0], 
         (void*)&col_low[block_stride], 
                sizeof(ClutterColor)*block_stride);
  memcpy((void*)&col_high[0], 
         (void*)&col_high[block_stride], 
                sizeof(ClutterColor)*block_stride);
  memcpy((void*)&col_low[block_stride*(height_block+1)], 
         (void*)&col_low[block_stride*height_block], 
                sizeof(ClutterColor)*block_stride);
  memcpy((void*)&col_high[block_stride*(height_block+1)], 
         (void*)&col_high[block_stride*height_block], 
                sizeof(ClutterColor)*block_stride);
        
  /* now assemble each block */
  out_data = (guint32*)compressed_data;  
  for (y=0;y<height_block;y++)
    {
      gint my; /* for morton numbers later */
      my = (y | (y << 8)) & 0x00FF00FF;
      my = (my | (my << 4)) & 0x0F0F0F0F;
      my = (my | (my << 2)) & 0x33333333;
      my = (my | (my << 1)) & 0x55555555;        
      for (x=0;x<width_block;x++)
        {
          ClutterColor *block;
          gint offs = x + y*block_stride;
          guint32 pixel_high_word = 0;
          guint32 pixel_low_word = 0;
          guint col_a, col_b;
          gint bx,by;
          gint mx, mz; /* for morton numbers later */
          
          /* now work out what every pixel should be... */
          block = (ClutterColor*)&uncompressed_data
                        [(x + y*width) * 4 * sizeof(guint32)];     
          /* find_best interpolates our two sets of colours to where they should 
           * be (the blocks we get colour from swap halfway through the block
           * hence the crazy offset stuff. It then figures out which one of the
           * 4 values for the pixel works best */ 
          for (by=3;by>=0;by--)
            for (bx=3;bx>=0;bx--)
              {   
                gint boffs = offs + ((bx+2)>>2) + (((by+2)>>2) * block_stride);
                pixel_low_word = (pixel_low_word << 2) | 
                          find_best(
                                  block[bx + by*width], 
                                  &col_low[boffs], 
                                  &col_high[boffs],
                                  block_stride,
                                  (bx+2)&3, 
                                  (by+2)&3);
              }
           /* pack our two colours */
           col_a = clutter_color_to_pvr_color(&col_low[offs+1+block_stride]);
           col_b = clutter_color_to_pvr_color(&col_high[offs+1+block_stride]);
           /* and finally pack into a block */
           /* last bit is the modulation mode, but we're cheating and
            * just going for the easy 0, 3/8, 5/8, 1 one */
           pixel_high_word = (col_b << 16) | (col_a & 0xFFFE);         
  
           /* PVR Stores images in a Morton arrangement to get some spatial 
            * locality
            * 
            * Interleave lower 16 bits of x and y, so the bits of x
            * are in the even positions and bits from y in the odd;
            * z gets the resulting 32-bit Morton Number. */        
           mx = (x | (x << 8)) & 0x00FF00FF;
           mx = (mx | (mx << 4)) & 0x0F0F0F0F;
           mx = (mx | (mx << 2)) & 0x33333333;
           mx = (mx | (mx << 1)) & 0x55555555;          
           mz = (my | (mx << 1)) & morton_mask;
           mz |= (x << xshift) & xmask;
           mz |= (y << yshift) & ymask;
           mz = mz << 1;
           
           /* write data out */
           out_data[mz  ] = pixel_low_word;
           out_data[mz+1] = pixel_high_word;
      }
    }
  
  g_free(col_low);
  g_free(col_high);
  return compressed_data;
}

/**
 * cogl_pvr_texture_decompress_pvrtc4:
 *
 * Returns an RGBA8888 bitmap created from decompressing the given compressed
 * data that was in PVRTC4 format...
 *
 * Since: 0.8.2-maemo
 */
guchar *cogl_pvr_texture_decompress_pvrtc4(
                const guchar *compressed_data, 
                gint width, 
                gint height)
{
  ClutterColor *uncompressed_data = 0;
  guint32 *compressed_datal = (guint32*)compressed_data;
  guint32 *arranged_data = 0; /* data after it has been rearranged */
  gint width_block, height_block, block_stride;
  ClutterColor *col_low, *col_high;
  gint x,y;
  guint32 morton_mask, xshift, xmask, yshift, ymask;
  /* must be a multiple of 4 + Power of 2 in each direction */
  if ((width&3) || (height&3) || 
      !cogl_util_is_power_2(width) ||
      !cogl_util_is_power_2(height))
    return 0;
    
  width_block = width / 4;
  block_stride = width_block+2;
  height_block = height / 4;  
  _calculate_access_masks(width_block, height_block,
      &morton_mask, &xshift, &xmask, &yshift, &ymask);
  /* 4 bits per pixel, or 64 bits per block*/
  uncompressed_data = g_malloc(sizeof(ClutterColor)*width*height);
  arranged_data = (guint32*)g_malloc(sizeof(guint32)*2*width_block*height_block);
  /* but we make our block colour list one bigger all the way around
   * and copy the colours so we don't need to do bounds checking */
  col_low = g_malloc(sizeof(ClutterColor)*block_stride*(height_block+2));
  col_high = g_malloc(sizeof(ClutterColor)*block_stride*(height_block+2));
  
  /* re-arrange data and  */
  for (y=0;y<height_block;y++)
    {
      /* space out Y bits ready for Morton pattern */
      gint my;
      gint offs = y*block_stride + block_stride;
      my = (y | (y << 8)) & 0x00FF00FF;
      my = (my | (my << 4)) & 0x0F0F0F0F;
      my = (my | (my << 2)) & 0x33333333;
      my = (my | (my << 1)) & 0x55555555;
         
      for (x=0;x<width_block;x++)
        {
          guint32 pixel_col_word = 0;
          gint mx, mz; /* for morton numbers later */
          
          /* PVR Stores images in Morton pattern to get some spatial 
          * locality
          * 
          * Interleave lower 16 bits of x and y, so the bits of x
          * are in the even positions and bits from y in the odd;
          * z gets the resulting 32-bit Morton Number. */        
          mx = (x | (x << 8)) & 0x00FF00FF;
          mx = (mx | (mx << 4)) & 0x0F0F0F0F;
          mx = (mx | (mx << 2)) & 0x33333333;
          mx = (mx | (mx << 1)) & 0x55555555;
          mz = (my | (mx << 1)) & morton_mask;
          mz |= (x << xshift) & xmask;
          mz |= (y << yshift) & ymask;
          mz = mz << 1;
         
          arranged_data[(x+(y*width_block))*2  ] = compressed_datal[mz  ];
          arranged_data[(x+(y*width_block))*2+1] = compressed_datal[mz+1];
          pixel_col_word = compressed_datal[mz+1];
          
          col_high[offs+x+1] = clutter_pvr_color_to_color(pixel_col_word >> 16);
          col_low[offs+x+1] = clutter_pvr_color_to_color(pixel_col_word & 0xFFFE);
        }
        
        col_low[offs] = col_low[offs+1];
        col_low[offs+block_stride+1] = col_low[offs+block_stride];  
        col_high[offs] = col_high[offs+1];
        col_high[offs+block_stride+1] = col_high[offs+block_stride];
      }
    /* copy top and bottom of our block so we get repeats */
    memcpy(&col_low[0], &col_low[block_stride], 
                sizeof(ClutterColor)*block_stride);
    memcpy(&col_high[0], &col_high[block_stride], 
                sizeof(ClutterColor)*block_stride);
    memcpy(&col_low[block_stride*(height_block+1)], 
           &col_low[block_stride*height_block], 
                sizeof(ClutterColor)*block_stride);
    memcpy(&col_high[block_stride*(height_block+1)], 
           &col_high[block_stride*height_block], 
                sizeof(ClutterColor)*block_stride);      
          
    for (y=0;y<height_block;y++)
      for (x=0;x<width_block;x++)
        {
          gint bx,by;
          gint offs = x + y*block_stride;      
          guint32 pixel_bits_word = arranged_data[(x+(y*width_block))*2];
          guint32 pixel_col_word  = arranged_data[(x+(y*width_block))*2 + 1];
          gboolean block_alpha_mode = pixel_col_word&1;
          /* now work out what every pixel in this block should be... */
          for (by=0;by<4;by++)
            for (bx=0;bx<4;bx++)
              {   
                ClutterColor tmpa, tmpb, cl, ch, col;     
                gint boffs = offs + ((bx+2)>>2) + (((by+2)>>2) * block_stride);
                gint pixel_bits;
                gint amtx, amty;
                              
                amtx = ((bx+2)&3) * 64;
                amty = ((by+2)&3) * 64;  
                pixel_bits = pixel_bits_word&3;
                pixel_bits_word = pixel_bits_word >> 2;
  
                clutter_color_interp(&col_low[boffs], 
                                &col_low[boffs+1], amtx, &tmpa);
                clutter_color_interp(&col_low[boffs+block_stride], 
                                &col_low[boffs+block_stride+1], amtx, &tmpb);
                clutter_color_interp(&tmpa, &tmpb, amty, &cl);
  
                clutter_color_interp(&col_high[boffs], 
                                &col_high[boffs+1], amtx, &tmpa);
                clutter_color_interp(&col_high[boffs+block_stride], 
                                &col_high[boffs+block_stride+1], amtx, &tmpb);
                clutter_color_interp(&tmpa, &tmpb, amty, &ch);
                
                if (block_alpha_mode) 
                  {
                    if (pixel_bits==0)
                      col = cl;
                    else if (pixel_bits==1)
                      clutter_color_interp(&cl, &ch, 128, &col);
                    else if (pixel_bits==2) {
                      clutter_color_interp(&cl, &ch, 128, &col);
                      col.alpha = 0;
                    } else col = ch;                    
                  }
                else
                  {
                    if (pixel_bits==0)
                      col = cl;
                    else if (pixel_bits==1)
                      clutter_color_interp(&cl, &ch, 96, &col);
                    else if (pixel_bits==2) {
                      clutter_color_interp(&cl, &ch, 160, &col);
                    } else col = ch;                              
                  } 
              uncompressed_data[(x*4) + (y*width*4) + bx + (by*width)]
                = col;
            }
      }  
  
  g_free(col_low);
  g_free(col_high);
  g_free(arranged_data);
  return (guchar*)uncompressed_data;
}

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
          
          uncompressed = cogl_pvr_texture_decompress_pvrtc4(
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
