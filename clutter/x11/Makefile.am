INCLUDES = \
	-DG_LOG_DOMAIN=\"ClutterX11\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/clutter 	        \
	-I$(top_builddir)/clutter 	        \
	$(CLUTTER_CFLAGS) \
	$(CLUTTER_DEBUG_CFLAGS) \
	$(GCC_FLAGS)

LDADD = $(CLUTTER_LIBS)

source_h = \
	clutter-x11-texture-pixmap.h \
	clutter-x11.h

BUILT_SOURCES = clutter-x11-enum-types.h clutter-x11-enum-types.c

clutter-x11-enum-types.h: stamp-clutter-x11-enum-types.h
	@true
stamp-clutter-x11-enum-types.h: $(source_h) Makefile
	( $(GLIB_MKENUMS) \
		--template $(srcdir)/clutter-x11-enum-types.h.in \
	  $(source_h) ) >> xgen-ceth && \
	(cmp xgen-ceth clutter-x11-enum-types.h || cp xgen-ceth clutter-x11-enum-types.h ) && \
	rm -f xgen-ceth && \
	echo timestamp > $(@F)

clutter-x11-enum-types.c: clutter-x11-enum-types.h
	( $(GLIB_MKENUMS) \
		--template $(srcdir)/clutter-x11-enum-types.c.in \
	  $(source_h) ) >> xgen-cetc && \
	cp xgen-cetc clutter-x11-enum-types.c && \
	rm -f xgen-cetc

noinst_LTLIBRARIES = libclutter-x11.la

libclutter_x11_la_SOURCES = \
	clutter-backend-x11.h \
	clutter-backend-x11.c \
	clutter-event-x11.c \
	clutter-stage-x11.h \
	clutter-stage-x11.c \
	clutter-x11-enum-types.h \
	clutter-x11-enum-types.c \
        clutter-x11-texture-pixmap.h \
        clutter-x11-texture-pixmap.c \
	XInput.h \
	clutter-x11.h

libclutter_x11_includedir = $(includedir)/clutter-@CLUTTER_API_VERSION@/clutter/x11
libclutter_x11_include_HEADERS = \
	clutter-x11.h \
	clutter-x11-enum-types.h \
	clutter-x11-texture-pixmap.h

clutter-x11-$(CLUTTER_API_VERSION).pc: clutter-x11.pc
	@cp -f $< $(@F)

pkgconfig_DATA = clutter-x11-@CLUTTER_API_VERSION@.pc
pkgconfigdir   = $(libdir)/pkgconfig

CLEANFILES = clutter-x11-$(CLUTTER_API_VERSION).pc

DISTCLEANFILES = \
	stamp-clutter-x11-enum-types.h \
	clutter-x11-enum-types.h \
	clutter-x11-enum-types.c

EXTRA_DIST = \
	clutter-x11.pc.in \
	clutter-x11-enum-types.h.in \
	clutter-x11-enum-types.c.in
