ACLOCAL_AMFLAGS = -I acinclude
SUBDIRS = @TOPLEVEL_SUBDIRS@

include $(top_srcdir)/build/common.mk
EXTRA_DIST = include

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ironbee.pc

doxygen doxygen-pdf:
	@(cd docs && $(MAKE) $@)

manual:
	@(cd docs && $(MAKE) manual)

.PHONY: doxygen doxygen-pdf manual
