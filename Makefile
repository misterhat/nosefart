CC = gcc
CFLAGS =
LDFLAGS = -lm -lSDL2
PREFIX = /usr
WANT_DEBUG=TRUE

NAME = nosefart
VERSION = 3.0.0

BUILDTOP = nsfobj
BUILDDIR = $(BUILDTOP)/build
SRCDIR = src

CFLAGS += -DNSF_PLAYER

ifeq "$(WANT_DEBUG)" "TRUE"
	CFLAGS += -ggdb
else
	CFLAGS += -O2 -fomit-frame-pointer -ffast-math -funroll-loops
	DEBUG_OBJECTS =
endif

CFLAGS +=\
 -I$(SRCDIR)\
 -I$(SRCDIR)/linux\
 -I$(SRCDIR)/sndhrdw\
 -I$(SRCDIR)/machine\
 -I$(SRCDIR)/cpu/nes6502\
 -I$(BUILDTOP)\
 -I/usr/local/include/

NSFINFO_CFLAGS = $(CFLAGS) -DNES6502_MEM_ACCESS_CTRL

FILES =\
 log\
 memguard\
 cpu/nes6502/nes6502\
 cpu/nes6502/dis6502\
 machine/nsf\
 sndhrdw/nes_apu\
 sndhrdw/vrcvisnd\
 sndhrdw/fmopl\
 sndhrdw/vrc7_snd\
 sndhrdw/mmc5_snd\
 sndhrdw/fds_snd

SRCS = $(addsuffix .c, $(FILES) linux/main_linux nsfinfo)
SOURCES = $(addprefix $(SRCDIR)/, $(SRCS))
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

ALL_OBJECTS = $(OBJECTS)
ALL_TARGETS = $(BUILDTOP)/$(NAME)

all: $(ALL_TARGETS)

$(BUILDDIR):
	mkdir -p $(sort $(dir $(ALL_OBJECTS)))

$(BUILDTOP)/config.h: $(BUILDDIR)
	echo "[$@]"
	echo "#define VERSION \"$(VERSION)\"" > $@
	echo "#define NAME \"$(NAME)\"" >> $@

$(BUILDDIR)/dep: $(BUILDTOP)/config.h
	$(CC) $(NSFINFO_CFLAGS) $(CFLAGS) -M $(SOURCES) > $@

-include $(BUILDDIR)/dep/

install: all
	mkdir -p $(PREFIX)/bin
	cp $(ALL_TARGETS) $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/$(NAME)

clean:
	rm -rf nsfobj

$(BUILDTOP)/$(NAME): $(OBJECTS)
	mkdir -p $(sort $(dir $(ALL_OBJECTS)))
	$(CC) $(NSFINFO_CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(BUILDTOP)/config.h
	mkdir -p $(sort $(dir $(ALL_OBJECTS)))
	$(CC)  $(NSFINFO_CFLAGS) -o $@ -c $<
