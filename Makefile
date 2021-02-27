MODULE_big	= pgnodemx
OBJS		= pgnodemx.o cgroup.o envutils.o fileutils.o genutils.o kdapi.o parseutils.o
PG_CPPFLAGS	= -I$(libpq_srcdir)
EXTENSION	= pgnodemx
DATA		= pgnodemx--1.0--1.1.sql pgnodemx--1.1.sql

GHASH := $(shell git rev-parse --short HEAD)

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pgnodemx
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

ifeq ($(strip $(VSTR)),)
ifneq ($(strip $(GHASH)),)
override CPPFLAGS += -DGIT_HASH=\"$(GHASH)\"
else
override CPPFLAGS += -DGIT_HASH=\"none\"
endif
else
override CPPFLAGS += -DGIT_HASH=\"$(VSTR)\"
endif
