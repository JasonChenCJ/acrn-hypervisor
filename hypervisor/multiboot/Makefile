#
# multiboot module makefile
#

MOD_C_SRCS += src/multiboot.c
ifeq ($(CONFIG_MULTIBOOT2),y)
MOD_C_SRCS += src/multiboot2.c
endif
MOD_C_OBJS := $(patsubst %.c,$(HV_OBJDIR)/multiboot/%.o,$(MOD_C_SRCS))

.PHONY: default
default: all

.PHONY: all
all:$(MULTIBOOT_MOD)

.PHONY: clean
clean:
	rm -rf $(HV_OBJDIR)/multiboot

$(MULTIBOOT_MOD): $(MOD_C_OBJS)
	$(AR) $(ARFLAGS) $(MULTIBOOT_MOD) $(MOD_C_OBJS)

-include dep/dep.mk

$(HV_OBJDIR)/multiboot/%.o: %.c
	[ ! -e $@ ] && mkdir -p $(dir $@) && mkdir -p $(HV_MODDIR); \
	$(CC) $(patsubst %, -I%, $(MOD_INC_PATH)) -I include -I dep -c $(CFLAGS) $(ARCH_CFLAGS) $< -o $@ -MMD -MT $@

