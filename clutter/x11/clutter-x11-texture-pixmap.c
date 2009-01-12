/*
 * Clutter.
 *
 * An OpenGL based 'interactive canvas' library.
 *
 * Authored By Johan Bilien  <johan.bilien@nokia.com>
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

/**
 * SECTION:clutter-x11-texture-pixmap
 * @short_description: A texture which displays the content of an X Pixmap.
 *
 * #ClutterX11TexturePixmap is a class for displaying the content of an
 * X Pixmap as a ClutterActor. Used together with the X Composite extension,
 * it allows to display the content of X Windows inside Clutter.
 *
 * The class uses the GLX_EXT_texture_from_pixmap OpenGL extension
 * (http://people.freedesktop.org/~davidr/GLX_EXT_texture_from_pixmap.txt)
 * if available
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../clutter-marshal.h"
#include "clutter-x11-texture-pixmap.h"
#include "clutter-x11.h"
#include "clutter-backend-x11.h"

#include "cogl/cogl.h"

/* We need this back on because the lack of notifications for Configure causes
 * textures not to update when they change size, and bug 95594 to revert
 */
#define XDAMAGE_HANDLING

#ifdef XDAMAGE_HANDLING
#include <X11/extensions/Xdamage.h>
#endif
#include <X11/extensions/Xcomposite.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <X11/extensions/XShm.h>

enum
{
  PROP_PIXMAP = 1,
  PROP_PIXMAP_WIDTH,
  PROP_PIXMAP_HEIGHT,
  PROP_DEPTH,
  PROP_AUTO,
  PROP_WINDOW,
  PROP_WINDOW_REDIRECT_AUTOMATIC,
  PROP_WINDOW_MAPPED,
  PROP_DESTROYED,
  PROP_WINDOW_X,
  PROP_WINDOW_Y,
  PROP_WINDOW_OVERRIDE_REDIRECT
};

enum
{
  UPDATE_AREA,
  /* FIXME: Pixmap lost signal? */
  LAST_SIGNAL
};

#ifdef XDAMAGE_HANDLING
static ClutterX11FilterReturn
on_x_event_filter (XEvent *xev, ClutterEvent *cev, gpointer data);
#endif

static void
clutter_x11_texture_pixmap_update_area_real (ClutterX11TexturePixmap *texture,
                                             gint                     x,
                                             gint                     y,
                                             gint                     width,
                                             gint                     height);
static void
clutter_x11_texture_pixmap_set_mapped (ClutterX11TexturePixmap *texture, gboolean mapped);
#ifdef XDAMAGE_HANDLING
static void
clutter_x11_texture_pixmap_destroyed (ClutterX11TexturePixmap *texture);
#endif

static guint signals[LAST_SIGNAL] = { 0, };

struct _ClutterX11TexturePixmapPrivate
{
  Window        window;
  Pixmap        pixmap;
  guint         pixmap_width, pixmap_height;
  guint         depth;

  XImage       *image;
  XShmSegmentInfo shminfo;

  gboolean      automatic_updates;
#ifdef XDAMAGE_HANDLING
  Damage        damage;
  Drawable      damage_drawable;
#endif

  /* FIXME: lots of gbooleans. coalesce into bitfields */
  gboolean	have_shm;
  gboolean      window_redirect_automatic;
  gboolean      window_mapped;
  gboolean      destroyed;
  gboolean      owns_pixmap;
  gboolean      override_redirect;
  gint          window_x, window_y;
};

#ifdef XDAMAGE_HANDLING
static int _damage_event_base = 0;
#endif

/* FIXME: Ultimatly with current cogl we should subclass clutter actor */
G_DEFINE_TYPE (ClutterX11TexturePixmap, \
               clutter_x11_texture_pixmap, \
               CLUTTER_TYPE_TEXTURE);

#ifdef XDAMAGE_HANDLING
static gboolean
check_extensions (ClutterX11TexturePixmap *texture)
{
  int                             damage_error;
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  priv = texture->priv;

  if (_damage_event_base)
    return TRUE;

  dpy = clutter_x11_get_default_display();

  if (!XDamageQueryExtension (dpy,
                              &_damage_event_base, &damage_error))
    {
      g_warning ("No Damage extension");
      return FALSE;
    }

  return TRUE;
}
#endif

static void
free_shm_resources (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;

  priv = texture->priv;

  if (priv->shminfo.shmid != -1)
    {
      XShmDetach(clutter_x11_get_default_display(),
		 &priv->shminfo);
      shmdt(priv->shminfo.shmaddr);
      shmctl(priv->shminfo.shmid, IPC_RMID, 0);
      priv->shminfo.shmid = -1;
    }
}

/* Tries to allocate enough shared mem to handle a full size
 * update size of the X Pixmap. */
static gboolean
try_alloc_shm (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate  *priv;
  XImage			  *dummy_image;
  Display			  *dpy;

  priv = texture->priv;
  dpy  = clutter_x11_get_default_display();

  g_return_val_if_fail (priv->pixmap, FALSE);

  if (!XShmQueryExtension(dpy) || g_getenv("CLUTTER_X11_NO_SHM"))
    {
      priv->have_shm = FALSE;
      return FALSE;
    }

  clutter_x11_trap_x_errors ();

  /* We are creating a dummy_image so we can have Xlib calculate
   * image->bytes_per_line - including any magic padding it may
   * want - for the largest possible ximage we might need to use
   * when handling updates to the texture.
   *
   * Note: we pass a NULL shminfo here, but that has no bearing
   * on the setup of the XImage, except that ximage->obdata will
   * == NULL.
   */
  dummy_image =
    XShmCreateImage(dpy,
		    DefaultVisual(dpy,
				  clutter_x11_get_default_screen()),
		    priv->depth,
		    ZPixmap,
		    NULL,
		    NULL, /* shminfo, */
		    priv->pixmap_width,
		    priv->pixmap_height);
  if (!dummy_image)
    goto failed_image_create;

  priv->shminfo.shmid = shmget (IPC_PRIVATE,
				dummy_image->bytes_per_line
				* dummy_image->height,
				IPC_CREAT|0777);
  if (priv->shminfo.shmid == -1)
    goto failed_shmget;

  priv->shminfo.shmaddr =
    shmat (priv->shminfo.shmid, 0, 0);
  if (priv->shminfo.shmaddr == (void *)-1)
    goto failed_shmat;

  priv->shminfo.readOnly = False;

  if (XShmAttach(dpy, &priv->shminfo) == 0)
    goto failed_xshmattach;

  if (clutter_x11_untrap_x_errors ())
    g_warning ("X Error: Failed to setup XShm");

  priv->have_shm = TRUE;
  return TRUE;

failed_xshmattach:
  g_warning ("XShmAttach failed");
  shmdt(priv->shminfo.shmaddr);
failed_shmat:
  g_warning ("shmat failed");
  shmctl(priv->shminfo.shmid, IPC_RMID, 0);
failed_shmget:
  g_warning ("shmget failed");
  XDestroyImage(dummy_image);
failed_image_create:

  if (clutter_x11_untrap_x_errors ())
    g_warning ("X Error: Failed to setup XShm");

  priv->have_shm = FALSE;
  return FALSE;
}

#ifdef XDAMAGE_HANDLING
static ClutterX11FilterReturn
on_x_event_filter (XEvent *xev, ClutterEvent *cev, gpointer data)
{
  ClutterX11TexturePixmap        *texture;
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  texture = CLUTTER_X11_TEXTURE_PIXMAP (data);

  g_return_val_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture), \
                        CLUTTER_X11_FILTER_CONTINUE);

  dpy = clutter_x11_get_default_display();
  priv = texture->priv;

  if (xev->type == _damage_event_base + XDamageNotify)
    {
      XserverRegion  parts;
      gint           i, r_count;
      XRectangle    *r_damage;
      XRectangle     r_bounds;
      XDamageNotifyEvent *dev = (XDamageNotifyEvent*)xev;

      if (dev->drawable != priv->damage_drawable)
        return CLUTTER_X11_FILTER_CONTINUE;


      clutter_x11_trap_x_errors ();
      /*
       * Retrieve the damaged region and break it down into individual
       * rectangles so we do not have to update the whole shebang.
       */
      parts = XFixesCreateRegion (dpy, 0, 0);
      XDamageSubtract (dpy, priv->damage, None, parts);

      r_damage = XFixesFetchRegionAndBounds (dpy,
                                             parts,
                                             &r_count,
                                             &r_bounds);

      clutter_x11_untrap_x_errors ();

      if (r_damage)
        {
          for (i = 0; i < r_count; ++i)
            clutter_x11_texture_pixmap_update_area (texture,
                                                    r_damage[i].x,
                                                    r_damage[i].y,
                                                    r_damage[i].width,
                                                    r_damage[i].height);
          g_debug("XDAMAGE END");
          XFree (r_damage);
        }

      XFixesDestroyRegion (dpy, parts);
    }

  return  CLUTTER_X11_FILTER_CONTINUE;
}

static ClutterX11FilterReturn
on_x_event_filter_too (XEvent *xev, ClutterEvent *cev, gpointer data)
{
  ClutterX11TexturePixmap        *texture;
  ClutterX11TexturePixmapPrivate *priv;

  texture = CLUTTER_X11_TEXTURE_PIXMAP (data);

  g_return_val_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture), \
                        CLUTTER_X11_FILTER_CONTINUE);

  priv = texture->priv;

  if (xev->xany.window != priv->window)
    return CLUTTER_X11_FILTER_CONTINUE;

  switch (xev->type) {
  case MapNotify:
  case ConfigureNotify:
    {
      /* Only sync the window pixmap if the size has changed */
      if (xev->xconfigure.width != priv->pixmap_width ||
          xev->xconfigure.height != priv->pixmap_height)
        clutter_x11_texture_pixmap_sync_window (texture);
      break;
    }
  case UnmapNotify:
    clutter_x11_texture_pixmap_set_mapped (texture, FALSE);
    break;
  case DestroyNotify:
    clutter_x11_texture_pixmap_destroyed (texture);
    break;
  default:
    break;
  }

  return CLUTTER_X11_FILTER_CONTINUE;
}
#endif


#ifdef XDAMAGE_HANDLING
static void
free_damage_resources (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;
  Display                        *dpy;

  priv = texture->priv;
  dpy = clutter_x11_get_default_display();

  if (priv->damage)
    {
      clutter_x11_trap_x_errors ();
      XDamageDestroy (dpy, priv->damage);
      XSync (dpy, FALSE);
      clutter_x11_untrap_x_errors ();
      priv->damage = None;
      priv->damage_drawable = None;
    }

  clutter_x11_remove_filter (on_x_event_filter, (gpointer)texture);
}
#endif

static void
clutter_x11_texture_pixmap_init (ClutterX11TexturePixmap *self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self,
                                   CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
                                   ClutterX11TexturePixmapPrivate);

#ifdef XDAMAGE_HANDLING
  if (!check_extensions (self))
    {
      /* FIMXE: means display lacks needed extensions for at least auto.
       *        - a _can_autoupdate() method ?
      */
    }
#endif

  self->priv->image = NULL;
  self->priv->automatic_updates = FALSE;
#ifdef XDAMAGE_HANDLING
  self->priv->damage = None;
  self->priv->damage_drawable = None;
#endif
  self->priv->window = None;
  self->priv->pixmap = None;
  self->priv->pixmap_height = 0;
  self->priv->pixmap_width = 0;
  self->priv->shminfo.shmid = -1;
  self->priv->window_redirect_automatic = TRUE;
  self->priv->window_mapped = FALSE;
  self->priv->destroyed = FALSE;
  self->priv->override_redirect = FALSE;
  self->priv->window_x = 0;
  self->priv->window_y = 0;
}

static void
clutter_x11_texture_pixmap_dispose (GObject *object)
{
  ClutterX11TexturePixmap *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

#ifdef XDAMAGE_HANDLING
  free_damage_resources (texture);

  clutter_x11_remove_filter (on_x_event_filter_too, (gpointer)texture);
#endif

  if (priv->owns_pixmap && priv->pixmap)
    {
      XFreePixmap (clutter_x11_get_default_display (), priv->pixmap);
      priv->pixmap = None;
    }

  if (priv->image)
    {
      XDestroyImage (priv->image);
      priv->image = NULL;
    }

  free_shm_resources (texture);

  G_OBJECT_CLASS (clutter_x11_texture_pixmap_parent_class)->dispose (object);
}

static void
clutter_x11_texture_pixmap_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  ClutterX11TexturePixmap  *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

  switch (prop_id)
    {
    case PROP_PIXMAP:
      clutter_x11_texture_pixmap_set_pixmap (texture,
                                             g_value_get_uint (value));
      break;
    case PROP_AUTO:
      clutter_x11_texture_pixmap_set_automatic (texture,
                                                g_value_get_boolean (value));
      break;
    case PROP_WINDOW:
      clutter_x11_texture_pixmap_set_window (texture,
                                             g_value_get_uint (value),
                                             priv->window_redirect_automatic);
      break;
    case PROP_WINDOW_REDIRECT_AUTOMATIC:
      {
        gboolean new;
        new = g_value_get_boolean (value);

        /* Change the update mode.. */
        if (new != priv->window_redirect_automatic && priv->window)
          clutter_x11_texture_pixmap_set_window (texture, priv->window, new);

        priv->window_redirect_automatic = new;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_x11_texture_pixmap_get_property (GObject      *object,
                                         guint         prop_id,
                                         GValue       *value,
                                         GParamSpec   *pspec)
{
  ClutterX11TexturePixmap *texture = CLUTTER_X11_TEXTURE_PIXMAP (object);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

  switch (prop_id)
    {
    case PROP_PIXMAP:
      g_value_set_uint (value, priv->pixmap);
      break;
    case PROP_PIXMAP_WIDTH:
      g_value_set_uint (value, priv->pixmap_width);
      break;
    case PROP_PIXMAP_HEIGHT:
      g_value_set_uint (value, priv->pixmap_height);
      break;
    case PROP_DEPTH:
      g_value_set_uint (value, priv->depth);
      break;
    case PROP_AUTO:
      g_value_set_boolean (value, priv->automatic_updates);
      break;
    case PROP_WINDOW:
      g_value_set_uint (value, priv->window);
      break;
    case PROP_WINDOW_REDIRECT_AUTOMATIC:
      g_value_set_boolean (value, priv->window_redirect_automatic);
      break;
    case PROP_WINDOW_MAPPED:
      g_value_set_boolean (value, priv->window_mapped);
      break;
    case PROP_DESTROYED:
      g_value_set_boolean (value, priv->destroyed);
      break;
    case PROP_WINDOW_X:
      g_value_set_int (value, priv->window_x);
      break;
    case PROP_WINDOW_Y:
      g_value_set_int (value, priv->window_y);
      break;
    case PROP_WINDOW_OVERRIDE_REDIRECT:
      g_value_set_boolean (value, priv->override_redirect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_x11_texture_pixmap_realize (ClutterActor *actor)
{
  ClutterX11TexturePixmap        *texture = CLUTTER_X11_TEXTURE_PIXMAP (actor);
  ClutterX11TexturePixmapPrivate *priv = texture->priv;

  CLUTTER_ACTOR_CLASS (clutter_x11_texture_pixmap_parent_class)->
      realize (actor);

  CLUTTER_ACTOR_SET_FLAGS (actor, CLUTTER_ACTOR_REALIZED);

  clutter_x11_texture_pixmap_update_area_real (texture,
					       0, 0,
					       priv->pixmap_width,
					       priv->pixmap_height);
}

static void
clutter_x11_texture_pixmap_class_init (ClutterX11TexturePixmapClass *klass)
{
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec        *pspec;
  ClutterBackend    *default_backend;

  g_type_class_add_private (klass, sizeof (ClutterX11TexturePixmapPrivate));

  object_class->dispose      = clutter_x11_texture_pixmap_dispose;
  object_class->set_property = clutter_x11_texture_pixmap_set_property;
  object_class->get_property = clutter_x11_texture_pixmap_get_property;

  actor_class->realize       = clutter_x11_texture_pixmap_realize;

  klass->update_area         = clutter_x11_texture_pixmap_update_area_real;

  pspec = g_param_spec_uint ("pixmap",
                             "Pixmap",
                             "The X11 Pixmap to be bound",
                             0, G_MAXINT,
                             None,
                             G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_PIXMAP, pspec);

  pspec = g_param_spec_uint ("pixmap-width",
                             "Pixmap width",
                             "The width of the "
                             "pixmap bound to this texture",
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_PIXMAP_WIDTH, pspec);

  pspec = g_param_spec_uint ("pixmap-height",
                             "Pixmap height",
                             "The height of the "
                             "pixmap bound to this texture",
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_PIXMAP_HEIGHT, pspec);

  pspec = g_param_spec_uint ("pixmap-depth",
                             "Pixmap Depth",
                             "The depth (in number of bits) of the "
                             "pixmap bound to this texture",
                             0, G_MAXUINT,
                             0,
                             G_PARAM_READABLE);

  g_object_class_install_property (object_class, PROP_DEPTH, pspec);

  pspec = g_param_spec_boolean ("automatic-updates",
                                "Automatic Updates",
                                "If the texture should be kept in "
                                "sync with any pixmap changes.",
                                FALSE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_AUTO, pspec);

  pspec = g_param_spec_uint ("window",
                             "Window",
                             "The X11 Window to be bound",
                             0, G_MAXINT,
                             None,
                             G_PARAM_READWRITE);

  g_object_class_install_property (object_class, PROP_WINDOW, pspec);

  pspec = g_param_spec_boolean ("window-redirect-automatic",
                                "Window Redirect Automatic",
                                "If composite window redirects are set to "
                                "Automatic (or Manual if false)",
                                TRUE,
                                G_PARAM_READWRITE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_REDIRECT_AUTOMATIC, pspec);


  pspec = g_param_spec_boolean ("window-mapped",
                                "Window Mapped",
                                "If window is mapped",
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_MAPPED, pspec);


  pspec = g_param_spec_boolean ("destroyed",
                                "Destroyed",
                                "If window has been destroyed",
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_DESTROYED, pspec);

  pspec = g_param_spec_int ("window-x",
                            "Window X",
                            "X position of window on screen according to X11",
                            G_MININT, G_MAXINT, 0, G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_X, pspec);


  pspec = g_param_spec_int ("window-y",
                            "Window Y",
                            "Y position of window on screen according to X11",
                            G_MININT, G_MAXINT, 0, G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_Y, pspec);

  pspec = g_param_spec_boolean ("window-override-redirect",
                                "Window Override Redirect",
                                "If this is an override-redirect window",
                                FALSE,
                                G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_WINDOW_OVERRIDE_REDIRECT, pspec);


  /**
   * ClutterX11TexturePixmap::update-area:
   * @texture: the object which received the signal
   *
   * The ::hide signal is emitted to ask the texture to update its
   * content from its source pixmap.
   *
   * Since: 0.8
   */
  signals[UPDATE_AREA] =
      g_signal_new ("update-area",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (ClutterX11TexturePixmapClass, \
                                     update_area),
                    NULL, NULL,
                    clutter_marshal_VOID__INT_INT_INT_INT,
                    G_TYPE_NONE, 4,
                    G_TYPE_INT,
                    G_TYPE_INT,
                    G_TYPE_INT,
                    G_TYPE_INT);

  default_backend = clutter_get_default_backend ();

  if (!CLUTTER_IS_BACKEND_X11 (default_backend))
    {
      g_critical ("ClutterX11TexturePixmap instanciated with a "
                  "non-X11 backend");
      return;
    }
}

static void
clutter_x11_texture_pixmap_update_area_real (ClutterX11TexturePixmap *texture,
                                             gint                     x,
                                             gint                     y,
                                             gint                     width,
                                             gint                     height)
{
  ClutterX11TexturePixmapPrivate       *priv;
  Display                              *dpy;
  XImage                               *image;
  char				       *first_pixel;
  GError                               *error = NULL;
  guint                                 bytes_per_line;
  char				       *data;
  gboolean                              data_allocated = FALSE;
  int                                   err_code;
  char                                  pixel_bpp;
  gboolean                              pixel_has_alpha;

#if 0
  clock_t start_t = clock();
#endif

  if (!CLUTTER_ACTOR_IS_REALIZED (texture))
    return;

  priv = texture->priv;
  dpy  = clutter_x11_get_default_display();

  if (!priv->pixmap)
    return;

  if (priv->shminfo.shmid == -1)
    try_alloc_shm (texture);

  clutter_x11_trap_x_errors ();

  if (priv->have_shm)
    {
      image =
	XShmCreateImage(dpy,
			DefaultVisual(dpy,
				      clutter_x11_get_default_screen()),
			priv->depth,
			ZPixmap,
			NULL,
			&priv->shminfo,
			width,
			height);
      image->data = priv->shminfo.shmaddr;

      XShmGetImage (dpy, priv->pixmap, image, x, y, AllPlanes);
      first_pixel = image->data;
    }
  else
    {
      if (!priv->image)
	{
          priv->image = XGetImage (dpy,
                                   priv->pixmap,
                                   0, 0,
                                   priv->pixmap_width, priv->pixmap_height,
                                   AllPlanes,
                                   ZPixmap);
	  first_pixel  = priv->image->data + priv->image->bytes_per_line * y
			  + x * priv->image->bits_per_pixel/8;
	}
      else
	{
          XGetSubImage (dpy,
                        priv->pixmap,
                        x, y,
                        width, height,
                        AllPlanes,
                        ZPixmap,
                        priv->image,
                        x, y);
	  first_pixel  = priv->image->data + priv->image->bytes_per_line * y
            + x * priv->image->bits_per_pixel/8;
	}
      image = priv->image;
    }

  XSync (dpy, FALSE);

  if ((err_code = clutter_x11_untrap_x_errors ()))
    {
      g_warning ("Failed to get XImage of pixmap: %lx, removing",
                 priv->pixmap);
      /* safe to assume pixmap has gone away? - therefor reset */
      clutter_x11_texture_pixmap_set_pixmap (texture, None);
      return;
    }

  if (priv->depth == 24)
    {
      bytes_per_line = image->bytes_per_line;
      data = first_pixel;
      pixel_bpp = 3;
      pixel_has_alpha = FALSE;
    }
  else if (priv->depth == 16)
    {
      bytes_per_line = image->bytes_per_line;
      data = first_pixel;
      pixel_bpp = 2;
      pixel_has_alpha = FALSE;
    }
  else if (priv->depth == 32)
    {
      bytes_per_line = image->bytes_per_line;
      data = first_pixel;
      pixel_bpp = 4;
      pixel_has_alpha = TRUE;
    }
  else
    return;

  /* For debugging purposes, un comment to simply generate dummy
   * pixmap data. (A Green background and Blue cross) */
#if 0
  {
    guint xpos, ypos;

    if (data_allocated)
      g_free (data);
    data_allocated = TRUE;
    data = g_malloc (width*height*4);
    bytes_per_line = width *4;

    for (ypos=0; ypos<height; ypos++)
      for (xpos=0; xpos<width; xpos++)
	{
	  char *p = data + width*4*ypos + xpos * 4;
	  guint32 *pixel = (guint32 *)p;
	  if ((xpos > width/2 && xpos <= (width/2) + width/4)
	      || (ypos > height/2 && ypos <= (height/2) + height/4))
	    *pixel=0xff0000ff;
	  else
	    *pixel=0xff00ff00;
	}
  }
#endif

  if (x != 0 || y != 0 ||
      width != priv->pixmap_width || height != priv->pixmap_height)
    clutter_texture_set_area_from_rgb_data  (CLUTTER_TEXTURE (texture),
					     (guint8 *)data,
					     pixel_has_alpha,
					     x, y,
					     width, height,
					     bytes_per_line,
					     pixel_bpp,
					     CLUTTER_TEXTURE_RGB_FLAG_BGR,
					     &error);
  else
    clutter_texture_set_from_rgb_data  (CLUTTER_TEXTURE (texture),
					(guint8 *)data,
					pixel_has_alpha,
					width, height,
					bytes_per_line,
					pixel_bpp,
					CLUTTER_TEXTURE_RGB_FLAG_BGR,
					&error);



  if (error)
    {
      g_warning ("Error when uploading from pixbuf: %s",
                 error->message);
      g_error_free (error);
    }

  if (data_allocated)
    g_free (data);


  if (priv->have_shm)
    XFree (image);
#if 0
  clock_t end_t = clock();
  int time = (int)((double)(end_t - start_t) * (1000.0 / CLOCKS_PER_SEC));
  g_print("clutter-x11-update-area-real(%d,%d,%d,%d) %d bits - %d ms\n",x,y,width,height,priv->depth,time);
#endif
}

/**
 * clutter_x11_texture_pixmap_new:
 *
 * Return value: A new #ClutterX11TexturePixmap
 *
 * Since: 0.8
 **/
ClutterActor *
clutter_x11_texture_pixmap_new (void)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP, NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_new_with_pixmap:
 * @pixmap: the X Pixmap to which this texture should be bound
 * @width: the width of the X pixmap
 * @height: the height of the X pixmap
 * @depth: the depth of the X pixmap
 *
 * Return value: A new #ClutterX11TexturePixmap bound to the given X Pixmap
 *
 * Since 0.8
 **/
ClutterActor *
clutter_x11_texture_pixmap_new_with_pixmap (Pixmap pixmap)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
			"pixmap", pixmap,
			NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_new_with_window:
 * @window: the X window to which this texture should be bound
 * @width: the width of the X pixmap
 * @height: the height of the X pixmap
 * @depth: the depth of the X pixmap
 *
 * Return value: A new #ClutterX11TexturePixmap bound to the given X window.
 *
 * Since 0.8
 **/
ClutterActor *
clutter_x11_texture_pixmap_new_with_window (Window window)
{
  ClutterActor *actor;

  actor = g_object_new (CLUTTER_X11_TYPE_TEXTURE_PIXMAP,
			"window", window,
			NULL);

  return actor;
}

/**
 * clutter_x11_texture_pixmap_set_pixmap:
 * @texture: the texture to bind
 * @pixmap: the X Pixmap to which the texture should be bound
 *
 * Sets the X Pixmap to which the texture should be bound.
 *
 * Since: 0.8
 **/
void
clutter_x11_texture_pixmap_set_pixmap (ClutterX11TexturePixmap *texture,
                                       Pixmap                   pixmap)
{
  Window       root;
  int          x, y;
  unsigned int width, height, border_width, depth;
  Status       status = 0;
  gboolean     new_pixmap = FALSE, new_pixmap_width = FALSE;
  gboolean     new_pixmap_height = FALSE, new_pixmap_depth = FALSE;

  ClutterX11TexturePixmapPrivate *priv;

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  clutter_x11_trap_x_errors ();

  status = XGetGeometry (clutter_x11_get_default_display(),
                         (Drawable)pixmap,
                         &root,
                         &x,
                         &y,
                         &width,
                         &height,
                         &border_width,
                         &depth);

  if (clutter_x11_untrap_x_errors () || status == 0)
    {
      if (pixmap != None)
        g_warning ("Unable to query pixmap: %lx", pixmap);
      pixmap = None;
      width = height = depth = 0;
    }

  if (priv->image)
    {
      XDestroyImage (priv->image);
      priv->image = NULL;
    }

  if (priv->pixmap != pixmap)
    {
      if (priv->pixmap && priv->owns_pixmap)
        XFreePixmap (clutter_x11_get_default_display (), priv->pixmap);

      priv->pixmap = pixmap;
      new_pixmap = TRUE;
    }

  if (priv->pixmap_width != width)
    {
      priv->pixmap_width = width;
      new_pixmap_width = TRUE;
    }

  if (priv->pixmap_height != height)
    {
      priv->pixmap_height = height;
      new_pixmap_height = TRUE;
    }

  if (priv->depth != depth)
    {
      priv->depth = depth;
      new_pixmap_depth = TRUE;
    }

  /* NB: We defer sending the signals until updating all the
   * above members so the values are all available to the
   * signal handlers. */
  g_object_ref (texture);
  if (new_pixmap)
    g_object_notify (G_OBJECT (texture), "pixmap");
  if (new_pixmap_width)
    g_object_notify (G_OBJECT (texture), "pixmap-width");
  if (new_pixmap_height)
    g_object_notify (G_OBJECT (texture), "pixmap-height");
  if (new_pixmap_depth)
    g_object_notify (G_OBJECT (texture), "pixmap-depth");

  free_shm_resources (texture);

  if (priv->depth != 0 &&
      priv->pixmap != None &&
      priv->pixmap_width != 0 &&
      priv->pixmap_height != 0)
    {
      if (CLUTTER_ACTOR_IS_REALIZED (texture))
        clutter_x11_texture_pixmap_update_area (texture,
                                                0, 0,
                                                priv->pixmap_width,
                                                priv->pixmap_height);

    }

  /*
   * Keep ref until here in case a notify causes removal from the scene; can't
   * lower the notifies because glx's notify handler needs to run before
   * update_area
   */
  g_object_unref (texture);
}

/**
 * clutter_x11_texture_pixmap_set_window:
 * @texture: the texture to bind
 * @window: the X window to which the texture should be bound
 * @automatic: TRUE is automatic window updates, FALSE for manual.
 *
 * Sets up a suitable pixmap for the window, using the composite and damage
 * extensions if possible, and then calls
 * clutter_x11_texture_pixmap_set_pixmap(). If you want a window in a texture,
 * you probably want this function, or its older sister,
 * clutter_glx_texture_pixmap_set_window().
 *
 * Since: 0.8
 **/
void
clutter_x11_texture_pixmap_set_window (ClutterX11TexturePixmap *texture,
                                       Window                   window,
                                       gboolean                 automatic)
{
  ClutterX11TexturePixmapPrivate *priv;
  XWindowAttributes attr;
  Display *dpy = clutter_x11_get_default_display ();

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  if (!clutter_x11_has_composite_extension())
    return;

  if (priv->window == window && automatic == priv->window_redirect_automatic)
    return;

#ifdef XDAMAGE_HANDLING
  if (priv->window)
    {
      clutter_x11_remove_filter (on_x_event_filter_too, (gpointer)texture);
      clutter_x11_trap_x_errors ();
      XCompositeUnredirectWindow(clutter_x11_get_default_display (),
                                  priv->window,
                                  priv->window_redirect_automatic ?
                                  CompositeRedirectAutomatic : CompositeRedirectManual);
      XSync (clutter_x11_get_default_display (), False);
      clutter_x11_untrap_x_errors ();
    }
#endif

  priv->window = window;
  priv->window_redirect_automatic = automatic;
  priv->window_mapped = FALSE;
  priv->destroyed = FALSE;

  if (window == None)
    return;

  clutter_x11_trap_x_errors ();
  {
    if (!XGetWindowAttributes (dpy, window, &attr))
      {
        XSync (dpy, False);
        clutter_x11_untrap_x_errors ();
        g_warning ("bad window 0x%x", (guint32)window);
        priv->window = None;
        return;
      }

#ifdef XDAMAGE_HANDLING
    XCompositeRedirectWindow
                       (dpy,
                        window,
                        automatic ?
                        CompositeRedirectAutomatic : CompositeRedirectManual);
#endif
    XSync (dpy, False);
  }

  clutter_x11_untrap_x_errors ();

#ifdef XDAMAGE_HANDLING
  if (priv->window)
    {
      XSelectInput (dpy, priv->window,
                    attr.your_event_mask | StructureNotifyMask);
      clutter_x11_add_filter (on_x_event_filter_too, (gpointer)texture);
    }
#endif

  g_object_ref (texture);
  g_object_notify (G_OBJECT (texture), "window");

  clutter_x11_texture_pixmap_set_mapped (texture,
                                         attr.map_state == IsViewable);

  clutter_x11_texture_pixmap_sync_window (texture);
  g_object_unref (texture);
}

/**
 * clutter_x11_texture_pixmap_sync_window:
 * @texture: the texture to bind
 *
 * Resets the texture's pixmap from its window, perhaps in response to the
 * pixmap's invalidation as the window changed size.
 *
 * Since: 0.8
 **/
void
clutter_x11_texture_pixmap_sync_window (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;
  Pixmap pixmap;

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  if (priv->destroyed)
    return;

  if (!clutter_x11_has_composite_extension())
    {
      clutter_x11_texture_pixmap_set_pixmap (texture, priv->window);
      return;
    }

  if (priv->window)
    {
      XWindowAttributes attr;
      Display *dpy = clutter_x11_get_default_display ();
      gboolean mapped, notify_x, notify_y, notify_override_redirect;

      /*
       * Make sure the window is mapped when getting the pixmap -- it's what
       * compiz does.
       */

      clutter_x11_trap_x_errors ();
      XGrabServer (dpy);

      XGetWindowAttributes (dpy, priv->window, &attr);
      mapped = attr.map_state == IsViewable;
      if (mapped)
        pixmap = XCompositeNameWindowPixmap (dpy, priv->window);
      else
        pixmap = None;

      XUngrabServer (dpy);
      clutter_x11_untrap_x_errors ();

      notify_x = attr.x != priv->window_x;
      notify_y = attr.y != priv->window_y;
      notify_override_redirect = attr.override_redirect != priv->override_redirect;
      priv->window_x = attr.x;
      priv->window_y = attr.y;
      priv->override_redirect = attr.override_redirect;

      g_object_ref (texture); /* guard against unparent */
      if (pixmap)
        {
          clutter_x11_texture_pixmap_set_pixmap (texture, pixmap);
          priv->owns_pixmap = TRUE;
        }
      clutter_x11_texture_pixmap_set_mapped (texture, mapped);
      /* could do more clever things with a signal, i guess.. */
      if (notify_override_redirect)
        g_object_notify (G_OBJECT (texture), "window-override-redirect");
      if (notify_x)
        g_object_notify (G_OBJECT (texture), "window-x");
      if (notify_y)
        g_object_notify (G_OBJECT (texture), "window-y");
      g_object_unref (texture);
    }
}

static void
clutter_x11_texture_pixmap_set_mapped (ClutterX11TexturePixmap *texture,
                                       gboolean mapped)
{
  ClutterX11TexturePixmapPrivate *priv;

  priv = texture->priv;

  if (mapped != priv->window_mapped)
    {
      priv->window_mapped = mapped;
      g_object_notify (G_OBJECT (texture), "window-mapped");
    }
}

#ifdef XDAMAGE_HANDLING
static void
clutter_x11_texture_pixmap_destroyed (ClutterX11TexturePixmap *texture)
{
  ClutterX11TexturePixmapPrivate *priv;

  priv = texture->priv;

  if (!priv->destroyed)
    {
      priv->destroyed = TRUE;
      g_object_notify (G_OBJECT (texture), "destroyed");
    }

  /*
   * Don't set window to None, that would destroy the pixmap, which might still
   * be useful e.g. for destroy animations -- app's responsibility.
   */
}
#endif

/**
 * clutter_x11_texture_pixmap_update_area:
 * @texture: The texture whose content shall be updated.
 * @x: the X coordinate of the area to update
 * @y: the Y coordinate of the area to update
 * @width: the width of the area to update
 * @height: the height of the area to update
 *
 * Performs the actual binding of texture to the current content of
 * the pixmap. Can be called to update the texture if the pixmap
 * content has changed.
 *
 * Since: 0.8
 **/
void
clutter_x11_texture_pixmap_update_area (ClutterX11TexturePixmap *texture,
                                        gint                     x,
                                        gint                     y,
                                        gint                     width,
                                        gint                     height)
{
  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  g_signal_emit (texture, signals[UPDATE_AREA], 0, x, y, width, height);
}

void
clutter_x11_texture_pixmap_set_automatic (ClutterX11TexturePixmap *texture,
                                          gboolean                 setting)
{
  ClutterX11TexturePixmapPrivate *priv;
#ifdef XDAMAGE_HANDLING
  Display                        *dpy;
#endif

  g_return_if_fail (CLUTTER_X11_IS_TEXTURE_PIXMAP (texture));

  priv = texture->priv;

  if (setting == priv->automatic_updates)
    return;

#ifdef XDAMAGE_HANDLING
  dpy = clutter_x11_get_default_display();

  if (setting == TRUE)
    {
      clutter_x11_add_filter (on_x_event_filter, (gpointer)texture);

      clutter_x11_trap_x_errors ();

      if (priv->window)
        priv->damage_drawable = priv->window;
      else
        priv->damage_drawable = priv->pixmap;

      priv->damage = XDamageCreate (dpy,
                                    priv->damage_drawable,
                                    XDamageReportNonEmpty);

      XSync (dpy, FALSE);
      clutter_x11_untrap_x_errors ();
    }
  else
    free_damage_resources (texture);
#endif

  priv->automatic_updates = setting;

}
