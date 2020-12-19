.PHONY: all debug release clean install

#
# These are often defined by the build
#

DESTDIR ?= /
UNITDIR ?= usr/lib/systemd/system

#
# Required packages
#

LIB_PKGS = libgbinder libglibutil libsystemd glib-2.0
PKGS = $(LIB_PKGS)

#
# Default target
#

all: debug release

#
# Sources
#

SRC = dummy_netd.c

#
# Directories
#

SRC_DIR = .
BUILD_DIR = build
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
DEBUG_FLAGS = -g
RELEASE_FLAGS =
DEBUG_DEFS = -DDEBUG
RELEASE_DEFS =
WARNINGS = -Wall -Wunused-result
FULL_CFLAGS = $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(LDFLAGS)

KEEP_SYMBOLS ?= 0
ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
SUBMAKE_OPTS += KEEP_SYMBOLS=1
endif

DEBUG_CFLAGS = $(DEBUG_FLAGS) -DDEBUG $(FULL_CFLAGS)
RELEASE_CFLAGS = $(RELEASE_FLAGS) -O2 $(FULL_CFLAGS)
DEBUG_LDFLAGS = $(DEBUG_FLAGS) $(FULL_LDFLAGS)
RELEASE_LDFLAGS = $(RELEASE_FLAGS) $(FULL_LDFLAGS)

LIBS = $(shell pkg-config --libs $(LIB_PKGS))
DEBUG_LIBS = $(LIBS)
RELEASE_LIBS = $(LIBS)

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = \
  $(DEBUG_OBJS:%.o=%.d) \
  $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

EXE = dummy_netd
DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)


debug: $(DEBUG_EXE)

release: $(RELEASE_EXE)

clean:
	rm -fr *~ */*~ rpm/*~ $(BUILD_DIR)

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

#
# Install
#

DESTDIR_ := $(shell echo $(DESTDIR)/ | sed -r 's|/+|/|g')

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d

INSTALL_SBIN_DIR = $(DESTDIR_)usr/sbin
INSTALL_SYSTEMD_DIR = $(DESTDIR_)$(UNITDIR)

install: $(RELEASE_EXE) $(INSTALL_SBIN_DIR) $(INSTALL_SYSTEMD_DIR)
	$(INSTALL) -m 644 dummy_netd.service $(INSTALL_SYSTEMD_DIR)
	$(INSTALL) -m 755 $(RELEASE_EXE) $(INSTALL_SBIN_DIR)

$(INSTALL_SBIN_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_SYSTEMD_DIR):
	$(INSTALL_DIRS) $@
