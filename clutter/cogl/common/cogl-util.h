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

#ifndef __COGL_UTIL_H
#define __COGL_UTIL_H

/* Help macros to scale from OpenGL <-1,1> coordinates system to our
 * X-window based <0,window-size> coordinates
 */
#define MTX_GL_SCALE_X(x,w,v1,v2) (CFX_QMUL (((CFX_QDIV ((x), (w)) + CFX_ONE) >> 1), (v1)) + (v2))
#define MTX_GL_SCALE_Y(y,w,v1,v2) ((v1) - CFX_QMUL (((CFX_QDIV ((y), (w)) + CFX_ONE) >> 1), (v1)) + (v2))
#define MTX_GL_SCALE_Y2(y,w,v1,v2) (CFX_QMUL (((CFX_QDIV ((y), (w)) + CFX_ONE) >> 1), (v1)) + (v2))
#define MTX_GL_SCALE_Z(z,w,v1,v2) (MTX_GL_SCALE_X ((z), (w), (v1), (v2)))

#define MTX_GL_SCALE_X_F(x,w,v1,v2) ((((((x)/(w)) + 1) / 2) * (v1)) + (v2))
#define MTX_GL_SCALE_Y2_F(y,w,v1,v2) ((((((y)/(w)) + 1) / 2) * (v1)) + (v2))
#define MTX_GL_SCALE_Z_F(z,w,v1,v2) (MTX_GL_SCALE_X_F ((z), (w), (v1), (v2)))

int
cogl_util_next_p2 (int a);

gboolean
cogl_util_is_power_2(int a);

guint32
cogl_util_log_2(guint v);

/* Transform point (x,y,z) by matrix */
void
cogl_util_mtx_transform (ClutterFixed m[16],
                         ClutterFixed *x, ClutterFixed *y, ClutterFixed *z,
                         ClutterFixed *w);
void
cogl_util_mtx_transform_f (float m[16],
                           float *x, float *y, float *z,
                           float *w);

ClutterVertex cogl_util_unproject(   ClutterFixed mtx[16],
                                     ClutterFixed mtx_p[16],
                                     ClutterFixed viewport[4],
                                     ClutterVertex obj_coord);
ClutterVertex cogl_util_unproject_f( float mtx[16],
                                     float mtx_p[16],
                                     ClutterFixed viewport[4],
                                     ClutterVertex obj_coord);

gboolean
cogl_check_extension (const gchar *name, const gchar *ext);

#endif /* __COGL_UTIL_H */
