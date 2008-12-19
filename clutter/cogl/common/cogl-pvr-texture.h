#ifndef COGLPVRTEXTURE_H_
#define COGLPVRTEXTURE_H_
/* handles loading of PVR texture files as a GLES texture */

#include "../cogl.h"

CoglHandle cogl_pvr_texture_load(const char *filename);

gboolean cogl_pvr_texture_save_pvrtc4(
                        const gchar *filename, 
                        const guchar *data, 
                        guint data_size, 
                        gint width, gint height);

guchar *cogl_pvr_texture_compress_pvrtc4(
                const guchar *uncompressed_data, 
                gint width, 
                gint height, 
                guint *compressed_size);
                
guchar *cogl_pvr_texture_decompress_pvrtc4(
                const guchar *compressed_data, 
                gint width, 
                gint height);                      

#endif /*COGLPVRTEXTURE_H_*/
