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

#ifndef COGL_PVR_TEXTURE_GL_H_
#define COGL_PVR_TEXTURE_GL_H_
/* handles loading of PVR texture files as a GLES texture */

#include "../cogl.h"
#include "pvr-texture.h"

CoglHandle cogl_pvr_texture_load(const char *filename);

#endif /*COGLPVRTEXTURE_H_*/
