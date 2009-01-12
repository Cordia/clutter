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
#include "cogl-util.h"

/**
 * cogl_util_next_p2:
 * @a: Value to get the next power
 *
 * Calculates the next power greater than @a.
 *
 * Return value: The next power after @a.
 */
int
cogl_util_next_p2 (int a)
{
  int rval=1;

  while(rval < a)
    rval <<= 1;

  return rval;
}

/**
 * cogl_util_is_power_2:
 * @a: Value to check
 *
 * Calculates if @a is a power of 2 or not. 0 is false
 *
 * Return value: Whether @a is a power of 2.
 */
gboolean
cogl_util_is_power_2(int a)
{
  return !(a & (a - 1)) && a;
}

/**
 * cogl_util_log_2:
 * @v: Value to check
 *
 * Calculates log2(@v)
 *
 * Return value: log2(@v)
 */
guint32
cogl_util_log_2(guint v)
{
  guint32 r; // result of log2(v) will go here
  guint32 shift;

  r =     (v > 0xFFFF) << 4; v >>= r;
  shift = (v > 0xFF  ) << 3; v >>= shift; r |= shift;
  shift = (v > 0xF   ) << 2; v >>= shift; r |= shift;
  shift = (v > 0x3   ) << 1; v >>= shift; r |= shift;
                                          r |= (v >> 1);
  return r;
}

/*
 * Utility functions for manipulating transformation matrix
 *
 * Matrix: 4x4 of ClutterFixed
 */
#define M(m,row,col)  (m)[(col) * 4 + (row)]

/* Transform point (x,y,z) by matrix */
void
cogl_util_mtx_transform (ClutterFixed m[16],
                         ClutterFixed *x, ClutterFixed *y, ClutterFixed *z,
                         ClutterFixed *w)
{
    ClutterFixed _x, _y, _z, _w;
    _x = *x;
    _y = *y;
    _z = *z;
    _w = *w;

    /* We care lot about precision here, so have to use QMUL */
    *x = CFX_QMUL (M (m, 0, 0), _x) + CFX_QMUL (M (m, 0, 1), _y) +
         CFX_QMUL (M (m, 0, 2), _z) + CFX_QMUL (M (m, 0, 3), _w);

    *y = CFX_QMUL (M (m, 1, 0), _x) + CFX_QMUL (M (m, 1, 1), _y) +
         CFX_QMUL (M (m, 1, 2), _z) + CFX_QMUL (M (m, 1, 3), _w);

    *z = CFX_QMUL (M (m, 2, 0), _x) + CFX_QMUL (M (m, 2, 1), _y) +
         CFX_QMUL (M (m, 2, 2), _z) + CFX_QMUL (M (m, 2, 3), _w);

    *w = CFX_QMUL (M (m, 3, 0), _x) + CFX_QMUL (M (m, 3, 1), _y) +
         CFX_QMUL (M (m, 3, 2), _z) + CFX_QMUL (M (m, 3, 3), _w);

    /* Specially for Matthew: was going to put a comment here, but could not
     * think of anything at all to say ;)
     */
}

#undef M

ClutterVertex cogl_util_unproject(   ClutterFixed mtx[16],
                                     ClutterFixed mtx_p[16],
                                     ClutterFixed viewport[4],
                                     ClutterVertex obj_coord)
{
  ClutterFixed           _w;
  ClutterVertex           res;

  res = obj_coord;
  _w = CFX_ONE;
  cogl_util_mtx_transform (mtx, &res.x, &res.y, &res.z, &_w);
  cogl_util_mtx_transform (mtx_p,
                           &res.x,
                           &res.y,
                           &res.z,
                           &_w);
  res.x = MTX_GL_SCALE_X (res.x, _w, viewport[2], viewport[0]);
  res.y = MTX_GL_SCALE_Y2 (res.y, _w, viewport[3], viewport[1]);
  res.z = MTX_GL_SCALE_Z (res.z, _w, viewport[2], viewport[0]);
  return res;
}
