all::

PRINTF=printf
FIND=find
SED=sed
XARGS=xargs
GETVERSION=git describe

ENABLE_CXX=yes
ENABLE_C99=yes

PREFIX=/usr/local

VWORDS:=$(shell src/getversion.sh --prefix=v MAJOR MINOR PATCH)
VERSION:=$(word 1,$(VWORDS))
BUILD:=$(word 2,$(VWORDS))

## Provide a version of $(abspath) that can cope with spaces in the
## current directory.
myblank:=
myspace:=$(myblank) $(myblank)
MYCURDIR:=$(subst $(myspace),\$(myspace),$(CURDIR)/)
MYABSPATH=$(foreach f,$1,$(if $(patsubst /%,,$f),$(MYCURDIR)$f,$f))

-include $(call MYABSPATH,config.mk)
-include udptun-env.mk

binaries.cc += udptun
udptun_obj += destinations
udptun_obj += emitters
udptun_obj += payloads
udptun_obj += queues
udptun_obj += config
udptun_obj += exits
udptun_obj += ears
udptun_obj += quotas
udptun_obj += main
udptun_obj += tunnels
udptun_obj += empties
udptun_obj += periods
udptun_obj += realtime
udptun_obj += events
udptun_obj += scheduling
udptun_obj += descriptor
udptun_obj += idle
udptun_obj += timed
udptun_lib += -lyaml-cpp

test_binaries.cc += testpayload
testpayload_obj += payloads
testpayload_obj += testpayload

include binodeps.mk

all:: installed-binaries VERSION BUILD


MYCMPCP=$(CMP) -s '$1' '$2' || $(CP) '$1' '$2'
.PHONY: prepare-version
mktmp:
	@$(MKDIR) tmp/
prepare-version: mktmp
	$(file >tmp/BUILD,$(BUILD))
	$(file >tmp/VERSION,$(VERSION))
BUILD: prepare-version
	@$(call MYCMPCP,tmp/BUILD,$@)
VERSION: prepare-version
	@$(call MYCMPCP,tmp/VERSION,$@)

YEARS=2002-3,2005-6,2012,2016

update-licence:
	$(FIND) . -name ".git" -prune -or -type f -print0 | $(XARGS) -0 \
	$(SED) -i 's/Copyright (C) [0-9,-]\+  Lancaster University/Copyright (C) $(YEARS)  Lancaster University/g'


tidy::
	@$(FIND) . -name "*~" -delete

distclean:: blank
	$(RM) VERSION BUILD
