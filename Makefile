OS := $(shell uname)

CXX = clang++-6.0
ifeq ($(OS), Linux)
ifndef CXX
CXX = clang++-6.0
endif
LDFLAGS= -Llib -lrt

endif

ifeq ($(OS), FreeBSD)
ifndef CXX
CXX = g++6
endif
LDFLAGS := -Llib -Wl,-rpath=/usr/local/lib/gcc6
endif

DEBUG = 1
WARNFLAGS= -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas 
CFLAGS = -std=c++11 $(WARNFLAGS)
ifeq ($(DEBUG), 1)
	CFLAGS += -ggdb
else
	CFLAGS += -O1
endif
#----------------------------
# Settings for power and arm arch
#----------------------------
ARCH := $(shell uname -a)
ifneq (,$(filter $(ARCH), powerpc64le ppc64le ))
	USE_SSE = 0
else
	USE_SSE=1
endif

ifndef USE_SSE
	USE_SSE = 1
endif

ifeq ($(USE_SSE), 1)
	CFLAGS += -msse2
endif

ifndef USE_RDMA
	USE_RDMA = 1
endif

ifeq ($(USE_RDMA), 1)
	LDFLAGS += -libverbs
endif

ifndef WITH_FPIC
	WITH_FPIC = 1
endif

ifeq ($(WITH_FPIC), 1)
	CFLAGS += -fPIC
endif

ifndef LINT_LANG
	LINT_LANG="all"
endif
PWD = $(shell pwd)
# All of the directories containing code.
INCFLAGS = -I$(PWD)/include
# build path
BUILD_DIR = ./build
# objectives that makes up rdc library
SLIB = lib/librdc.so
ALIB = lib/librdc.a
DEFS += -DLOGGING_WITH_STREAMS=1 -DRDC_USE_BASE

ifeq ($(USE_RDMA), 1)
	DEFS += -DRDC_USE_RDMA
endif

CFLAGS += $(DEFS)
DMLC=./

SRCS = $(wildcard src/*/*/*.cc src/*/*.cc src/*.cc)
SRC_DIRS = $(notdir $(SRCS))
OBJS = $(patsubst %.cc, build/%.o, $(SRC_DIRS))
DEPS = $(patsubst %.cc, build/%.d, $(SRC_DIRS))

all : $(SLIB) test
.PHONY: clean all install python lint doc doxygen

build/%.o:src/*/*/%.cc
	mkdir -p $(@D)
	$(CXX) -c $(CFLAGS) $(INCFLAGS) $< -o $@
	$(CXX) -std=c++11 -c $(CFLAGS) $(INCFLAGS) -MMD -c $< -o $@
build/%.o:src/*/%.cc
	mkdir -p $(@D)
	$(CXX) -c $(CFLAGS) $(INCFLAGS) $< -o $@
	$(CXX) -std=c++11 -c $(CFLAGS) $(INCFLAGS) -MMD -c $< -o $@

#$(ALIB):
#	ar cr $@ $(filter %.o, $^)
#
$(SLIB) : $(OBJS)
	mkdir -p $(@D)
	$(CXX) $(CFLAGS) -shared -o $@ $(filter %.cpp %.o %.c %.cc %.a, $^)
#
#lint:
#	$(DMLC)/scripts/lint.py rdc $(LINT_LANG) src include
#
#doc doxygen:
#	cd include; doxygen ../doc/Doxyfile; cd -
#
-include build/*.d

install:
	cp $(SLIB) /usr/local/lib
include tests/test.mk
test: $(TESTS)
clean:
	$(RM) $(OBJS) $(DEPS) $(ALIB) $(SLIB) $(TESTS)
