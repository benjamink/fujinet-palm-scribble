CC = m68k-palmos-gcc
CFLAGS = -O2 -Wall -palmos3.5

FNLIB ?= ../../../../fujinet-lib-palmos
COMMON = ../common
INCLUDES = -I$(FNLIB)/include -I$(COMMON)
LIBDIR = $(FNLIB)/r2r/palmos
LIBS = -L$(LIBDIR) -lfujinet.palmos

# Host-side unit tests (plain host cc, no Palm toolchain). Add further test
# binaries to TESTS and give each a rule below.
HOSTCC ?= cc
HOSTFLAGS = -std=c89 -Wall -Wextra
TESTS = test_sketch test_proto test_netutil test_selection test_canvas

scribble.prc: scribble scribble.def bin.stamp
	build-prc -o $@ scribble.def scribble *.bin

APPSRC = scribble.c net.c netutil.c proto.c sketch.c selection.c canvas.c $(COMMON)/fnapp.c
APPHDR = scribble_rsc.h net.h netutil.h proto.h sketch.h selection.h canvas.h $(COMMON)/fnapp.h

scribble: $(APPSRC) $(APPHDR) $(LIBDIR)/libfujinet.palmos.a
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(APPSRC) $(LIBS)

$(LIBDIR)/libfujinet.palmos.a:
	$(MAKE) -C $(FNLIB) palmos

bin.stamp: scribble.rcp scribble_rsc.h
	pilrc -q scribble.rcp
	touch $@

test_sketch: test_sketch.c sketch.c sketch.h
	$(HOSTCC) $(HOSTFLAGS) -o $@ test_sketch.c sketch.c

test_proto: test_proto.c proto.c proto.h sketch.c sketch.h
	$(HOSTCC) $(HOSTFLAGS) -o $@ test_proto.c proto.c sketch.c

test_netutil: test_netutil.c netutil.c netutil.h proto.c proto.h sketch.h
	$(HOSTCC) $(HOSTFLAGS) -o $@ test_netutil.c netutil.c proto.c

test_selection: test_selection.c selection.c selection.h
	$(HOSTCC) $(HOSTFLAGS) -o $@ test_selection.c selection.c

test_canvas: test_canvas.c canvas.c canvas.h sketch.c sketch.h
	$(HOSTCC) $(HOSTFLAGS) -o $@ test_canvas.c canvas.c sketch.c

test: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done

clean:
	rm -f scribble scribble.prc *.bin bin.stamp $(TESTS)

.PHONY: test clean
