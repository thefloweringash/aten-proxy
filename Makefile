CFLAGS := -std=c++11  -Wall -Wextra -Werror -g
LDFLAGS := -stdlib=libc++

LIBVNCSERVER_CFLAGS ?= $(shell pkg-config --cflags libvncserver)
LIBVNCSERVER_LDFLAGS ?= $(shell pkg-config --libs libvncserver)
CFLAGS += $(LIBVNCSERVER_CFLAGS)
LDFLAGS += $(LIBVNCSERVER_LDFLAGS)

LIBEV_CFLAGS ?=
LIBEV_LDFLAGS ?= -lev
CFLAGS += $(LIBEV_CFLAGS)
LDFLAGS += $(LIBEV_LDFLAGS)

OBJDIR := obj
SRC := main.cc keymap.cc
OBJ := $(addprefix $(OBJDIR)/,$(SRC:.cc=.o))
DEP := $(addprefix $(OBJDIR)/,$(SRC:.cc=.d))


.PHONY: all
all: aten-proxy

.PHONY: clean
clean:
	rm -f $(OBJ)

aten-proxy: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.cc
	$(CXX) -MMD $(CFLAGS) -c -o $@ $<

$(OBJ): | $(OBJDIR)/.made

$(OBJDIR)/.made:
	mkdir -p $(OBJDIR) && touch $@

-include $(DEP)
