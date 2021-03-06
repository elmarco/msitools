NULL =
ACLOCAL_AMFLAGS = -I m4
SUBDIRS = include data libmsi tests po .

libmsiincludedir = $(includedir)/libmsi-1.0
dist_libmsiinclude_HEADERS =			\
	include/libmsi-database.h		\
	include/libmsi-enums.h			\
	include/libmsi-query.h			\
	include/libmsi-record.h			\
	include/libmsi-summary-info.h		\
	include/libmsi-types.h			\
	include/libmsi.h			\
	$(NULL)

AM_CPPFLAGS = -Iinclude -I$(srcdir)/include	\
	$(GLIB_CFLAGS) $(GSF_CFLAGS) $(UUID_CFLAGS) \
	$(NULL)

AM_LDFLAGS = -Llibmsi

# Low-level tools

bin_PROGRAMS = msibuild msiinfo

msibuild_SOURCES = tools/msibuild.c tools/sqldelim.c tools/sqldelim.h
msibuild_LDADD = -lmsi $(GLIB_LIBS) $(GSF_LIBS) $(UUID_LIBS)
msibuild_DEPENDENCIES = libmsi/libmsi.la

msiinfo_SOURCES = tools/msiinfo.c
msiinfo_LDADD = -lmsi $(GLIB_LIBS) $(GSF_LIBS)
msiinfo_DEPENDENCIES = libmsi/libmsi.la

# msiextract, something on the TODO list...

noinst_PROGRAMS = msiextract

msiextract_CFLAGS = -w

# --vapidir paths are relative to the source directory!

msiextract_VALAFLAGS =				\
	--enable-experimental			\
	--pkg config				\
	--pkg gio-unix-2.0			\
	--pkg libgcab-1.0			\
	--pkg libmsi-1.0			\
	--pkg posix				\
	--vapidir=.				\
	--vapidir=$(abs_top_builddir)/libmsi	\
	$(NULL)

msiextract_SOURCES =				\
	tools/msiextract.vala			\
	$(NULL)

msiextract_CPPFLAGS = $(wixl_CPPFLAGS)
msiextract_LDADD = $(wixl_LDADD)

# Wixl

bin_PROGRAMS += wixl

wixl_CFLAGS = -w

wixl_VALAFLAGS =				\
	-H wixl.h --use-header			\
	--vapidir=.				\
	--vapidir=vapi				\
	--vapidir=$(abs_top_builddir)/libmsi	\
	--pkg config				\
	--enable-experimental			\
	--pkg gio-2.0				\
	--pkg libmsi-1.0			\
	--pkg libgcab-1.0			\
	--pkg libxml-2.0			\
	--pkg posix				\
	$(NULL)

wixl_SOURCES =					\
	tools/wixl/builder.vala			\
	tools/wixl/msi-default.vala		\
	tools/wixl/msi.vala			\
	tools/wixl/preprocessor.vala		\
	tools/wixl/util.vala			\
	tools/wixl/wix.vala			\
	tools/wixl/wixl.vala			\
	$(NULL)

wixl_CPPFLAGS =					\
	-include config.h			\
	-I $(top_srcdir)/include		\
	$(WIXL_CFLAGS)				\
	-DG_LOG_DOMAIN=\""wixl"\"		\
	-DLOCALEDIR=\""$(localedir)"\"		\
	-DPKGDATADIR=\""$(datadir)/wixl-$(VERSION)/"\"	\
	-DPKGLIBDIR=\""$(pkglibdir)"\"		\
	$(NULL)

wixl_LDADD =					\
	$(WIXL_LIBS)				\
	libmsi/libmsi.la			\
	$(NULL)

wixl_DEPENDENCIES = libmsi/libmsi.la

if !WIN32
bin_PROGRAMS += wixl-heat

wixl_heat_CFLAGS = -w

wixl_heat_VALAFLAGS =				\
	--vapidir=.				\
	--pkg config				\
	--enable-experimental			\
	--pkg gio-unix-2.0			\
	$(NULL)

wixl_heat_SOURCES =				\
	tools/wixl/wixl-heat.vala		\
	tools/wixl/util.vala			\
	$(NULL)

wixl_heat_CPPFLAGS = $(wixl_CPPFLAGS)
wixl_heat_LDADD = $(wixl_LDADD)
endif

# Autotest support

dist_noinst_DATA = tests/testsuite.at tests/wixl.at tests/package.m4 tests/testsuite
DISTCLEANFILES = atconfig atlocal
CLEANFILES = testsuite.log

check-local: $(srcdir)/tests/testsuite atconfig atlocal
	$(SHELL) $(srcdir)/tests/testsuite AUTOTEST_PATH=. $(TESTSUITEFLAGS)

installcheck-local: $(srcdir)/tests/testsuite atconfig atlocal
	$(SHELL) $(srcdir)/tests/testsuite AUTOTEST_PATH=$(bindir) $(TESTSUITEFLAGS)

clean-local:
	-$(SHELL) $(srcdir)/tests/testsuite --clean

dist-hook:
	@for file in `cd $(srcdir)/tests && \
		$(AUTOM4TE) --language=autotest --trace='AT_MSIDATA:tests/data/$$1' \
			    testsuite.at`; do \
	    dir=`echo "/$$file" | sed -e 's,/[^/]*$$,,'`; \
	    $(MKDIR_P) "$(distdir)$$dir" || exit 1; \
	    echo cp -fpR $(srcdir)/$$file "$(distdir)$$dir"; \
	    cp -fpR $(srcdir)/$$file "$(distdir)$$dir" || exit 1; \
	done


$(srcdir)/tests/testsuite: tests/testsuite.at tests/wixl.at tests/package.m4
	cd $(srcdir)/tests && \
		$(AUTOM4TE) --language=autotest -o testsuite.tmp testsuite.at && \
		mv testsuite.tmp testsuite

EXTRA_DIST = msitools.doap build-aux/move-if-change autogen.sh
