XBE_TITLE = ModernGekkoX\ loader\ diagnostic
GEN_XISO =
SRCS = $(CURDIR)/main.c $(CURDIR)/../src/mgx_dol.c
CFLAGS += -std=c11 -O2 -Wall -Wextra -I$(CURDIR)/../include
ifndef NXDK_DIR
$(error Set NXDK_DIR to the pinned nxdk checkout)
endif
include $(NXDK_DIR)/Makefile
