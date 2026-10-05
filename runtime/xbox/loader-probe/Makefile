XBE_TITLE = ModernGekkoX\ loader\ diagnostic
GEN_XISO =
SRCS = $(CURDIR)/main.c $(CURDIR)/../src/mgx_dol.c
# nxdk inherits CFLAGS for SDK sources, which use GNU inline assembly.
CFLAGS += -std=gnu11 -O2 -Wall -Wextra -I$(CURDIR)/../include
ifndef NXDK_DIR
$(error Set NXDK_DIR to the pinned nxdk checkout)
endif
include $(NXDK_DIR)/Makefile
