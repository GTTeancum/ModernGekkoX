XBE_TITLE = ModernGekkoX\ math\ diagnostic
GEN_XISO =
SRCS = $(CURDIR)/main.c $(CURDIR)/../src/mgx_math.c
CFLAGS += -std=gnu11 -O2 -fno-fast-math -ffp-contract=off -frounding-math -I$(CURDIR)/../include
ifndef NXDK_DIR
$(error Set NXDK_DIR to the pinned nxdk checkout)
endif
include $(NXDK_DIR)/Makefile
