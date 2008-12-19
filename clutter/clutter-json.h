#ifndef __CLUTTER_JSON_H__
#define __CLUTTER_JSON_H__

/* Include json-glib types opaquely, so that we can swap out
 * the internal copy of JSON-GLib with the installed one
 * without changing the other headers.
 */

#include "json/json-types.h"
#include "json/json-parser.h"
#include "json/json-generator.h"

#endif /* __CLUTTER_JSON_H__ */
