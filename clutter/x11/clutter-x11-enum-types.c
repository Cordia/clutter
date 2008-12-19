
/* Generated data (by glib-mkenums) */

#include "clutter-x11-enum-types.h"
/* enumerations from "clutter-x11.h" */
#include "clutter-x11.h"
GType
clutter_x11_filter_return_get_type(void) {
  static GType etype = 0;
  if (G_UNLIKELY (!etype))
    {
      static const GEnumValue values[] = {
        { CLUTTER_X11_FILTER_CONTINUE, "CLUTTER_X11_FILTER_CONTINUE", "continue" },
        { CLUTTER_X11_FILTER_TRANSLATE, "CLUTTER_X11_FILTER_TRANSLATE", "translate" },
        { CLUTTER_X11_FILTER_REMOVE, "CLUTTER_X11_FILTER_REMOVE", "remove" },
        { 0, NULL, NULL }
      };
      etype = g_enum_register_static (g_intern_static_string ("ClutterX11FilterReturn"), values);
    }
  return etype;
}

GType
clutter_x11_xinput_event_types_get_type(void) {
  static GType etype = 0;
  if (G_UNLIKELY (!etype))
    {
      static const GEnumValue values[] = {
        { CLUTTER_X11_XINPUT_KEY_PRESS_EVENT, "CLUTTER_X11_XINPUT_KEY_PRESS_EVENT", "key-press-event" },
        { CLUTTER_X11_XINPUT_KEY_RELEASE_EVENT, "CLUTTER_X11_XINPUT_KEY_RELEASE_EVENT", "key-release-event" },
        { CLUTTER_X11_XINPUT_BUTTON_PRESS_EVENT, "CLUTTER_X11_XINPUT_BUTTON_PRESS_EVENT", "button-press-event" },
        { CLUTTER_X11_XINPUT_BUTTON_RELEASE_EVENT, "CLUTTER_X11_XINPUT_BUTTON_RELEASE_EVENT", "button-release-event" },
        { CLUTTER_X11_XINPUT_MOTION_NOTIFY_EVENT, "CLUTTER_X11_XINPUT_MOTION_NOTIFY_EVENT", "motion-notify-event" },
        { CLUTTER_X11_XINPUT_LAST_EVENT, "CLUTTER_X11_XINPUT_LAST_EVENT", "last-event" },
        { 0, NULL, NULL }
      };
      etype = g_enum_register_static (g_intern_static_string ("ClutterX11XInputEventTypes"), values);
    }
  return etype;
}

GType
clutter_x11_input_device_type_get_type(void) {
  static GType etype = 0;
  if (G_UNLIKELY (!etype))
    {
      static const GEnumValue values[] = {
        { CLUTTER_X11_XINPUT_POINTER_DEVICE, "CLUTTER_X11_XINPUT_POINTER_DEVICE", "pointer-device" },
        { CLUTTER_X11_XINPUT_KEYBOARD_DEVICE, "CLUTTER_X11_XINPUT_KEYBOARD_DEVICE", "keyboard-device" },
        { CLUTTER_X11_XINPUT_EXTENSION_DEVICE, "CLUTTER_X11_XINPUT_EXTENSION_DEVICE", "extension-device" },
        { 0, NULL, NULL }
      };
      etype = g_enum_register_static (g_intern_static_string ("ClutterX11InputDeviceType"), values);
    }
  return etype;
}


/* Generated data ends here */

