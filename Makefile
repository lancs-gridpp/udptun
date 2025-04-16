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
udptun_obj += clientid
udptun_obj += sockaddrs
udptun_obj += destinations
udptun_obj += destbank
udptun_obj += peers
udptun_obj += addrent
udptun_obj += logging
udptun_obj += marshaling
udptun_obj += streamer
udptun_obj += channels
udptun_obj += absorbers
udptun_obj += udp_absorbers
udptun_obj += signaling
udptun_obj += addressing
udptun_obj += emitters
udptun_obj += payloads
udptun_obj += queues
udptun_obj += config
udptun_obj += exits
udptun_obj += egress
udptun_obj += tcp_egress
udptun_obj += quotas
udptun_obj += main
udptun_obj += ingress
udptun_obj += tcp_ingress
udptun_obj += empties
udptun_obj += events
udptun_obj += scheduling
udptun_obj += descriptor
udptun_obj += idle
udptun_obj += timed
udptun_obj += fnexp
udptun_obj += network
udptun_lib += -lyaml-cpp
udptun_lib += -lz

test_binaries.cc += testpayload
testpayload_obj += payloads
testpayload_obj += testpayload

include binodeps.mk

all:: installed-binaries VERSION BUILD

install:: install-binaries


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

YEARS=2025

update-licence:
	$(FIND) . -name ".git" -prune -or -type f -print0 | $(XARGS) -0 \
	$(SED) -i 's/Copyright (C) [0-9,-]\+  Lancaster University/Copyright (C) $(YEARS)  Lancaster University/g'


tidy::
	@$(FIND) . -name "*~" -delete

distclean:: blank
	$(RM) VERSION BUILD
