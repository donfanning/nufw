if BUILD_EMC

AM_CFLAGS = -DLOCAL_STATE_DIR=\"$(localstatedir)\" -DCONFIG_DIR=\"$(sysconfdir)\"

INCLUDES = $(GLIB_CFLAGS) -I$(top_srcdir)/src/include/ -I$(top_srcdir)/src/libs/nubase/ -I$(top_srcdir)/src/libs/nussl/ -I$(top_srcdir)/src/libs/nubase -I$(top_srcdir)/src/libs/nuconfparser -D_REENTRANT

bin_PROGRAMS = emc

noinst_emc_SOURCES = emc_server.h emc_config.h \
	emc_tls.h emc_worker.h \
	emc_data_parser.h \
	emc_directory.h \
	emc_proto.h

emc_SOURCES = emc_main.c \
	emc_server.c emc_config.c \
	emc_tls.c emc_worker.c \
	emc_data_parser.c \
	emc_directory.c \
	${noinst_emc_SOURCES}

emc_LDADD = $(GLIB_LIBS) ../libs/nussl/libnussl.la ../libs/nuconfparser/libnuconfparser.la ../libs/nubase/libnubase.la -lev

install-exec-local:
	install -d "$(DESTDIR)$(localstatedir)/run/emc/"

endif
