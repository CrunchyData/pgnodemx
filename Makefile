ifdef USE_PGXS
PG_CONFIG = pg_config
datadir := $(shell $(PG_CONFIG) --sharedir)
else
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
endif

MODULE_big	= pgnodemx
OBJS		= pgnodemx.o cgroup.o envutils.o fileutils.o genutils.o kdapi.o parseutils.o procfunc.o
PG_CPPFLAGS	= -I$(libpq_srcdir)
PATH_TO_FILE	= $(datadir)/extension/pg_proctab.control
ifeq ($(shell test -e $(PATH_TO_FILE) && echo -n yes),yes)
EXTENSION	= pgnodemx pg_proctab--0.0.10-compat
else
EXTENSION	= pgnodemx pg_proctab--0.0.10-compat pg_proctab
endif
DATA		= pgnodemx--1.0--1.1.sql pgnodemx--1.1--1.2.sql pgnodemx--1.2--1.3.sql pgnodemx--1.3--1.4.sql pgnodemx--1.4.sql pg_proctab--0.0.10-compat.sql

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
