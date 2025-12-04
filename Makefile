TOPDIR=$(HOME)/f-stack

ifeq ($(FF_PATH),)
	FF_PATH=${TOPDIR}
endif

ifneq ($(shell pkg-config --exists libdpdk && echo 0),0)
$(error "No installation of DPDK found, maybe you should export environment variable `PKG_CONFIG_PATH`")
endif

PKGCONF ?= pkg-config

CXXFLAGS += -O3 -g -gdwarf-2 --std=c++20 $(shell $(PKGCONF) --cflags libdpdk) -I${TOPDIR}/lib/

LIBS+= -L${FF_PATH}/lib -Wl,--whole-archive,-lfstack,--no-whole-archive
LIBS+= -Wl,--no-whole-archive -lrt -lm -ldl -lcrypto -pthread -lnuma
LIBS+= $(shell $(PKGCONF) --libs libdpdk)

TARGET_FORWARD="main_forward"
TARGET_RECV="receive"
all:
	cc ${CXXFLAGS} -DINET6 -o ${TARGET_FORWARD} forward.cc ${LIBS}
	cc ${CXXFLAGS} -o ${TARGET_RECV} receive.cc ${LIBS}

.PHONY: clean
clean:
	rm -f *.o 
