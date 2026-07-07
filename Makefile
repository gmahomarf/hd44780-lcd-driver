
# LDDINC=$(PWD)/../include
# ccflags-y += -I$(LDDINC)

ifeq ($(KERNELRELEASE),)

    # Assume the source tree is where the running kernel was built
    # You should set KERNELDIR in the environment if it's elsewhere
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    # The current directory is passed to sub-makes as argument
    SRCDIR := $(shell pwd)/src

    _ARCH_INCLUDE :=
    ifneq ($(ARCH),)
        _ARCH_INCLUDE := -I$(KERNELDIR)/arch/$(ARCH)/include -I$(KERNELDIR)/arch/$(ARCH)/include/generated
    endif

modules:
	$(MAKE) -C $(KERNELDIR) M=$(SRCDIR) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(SRCDIR) modules_install

clean:
	cd src; \
	rm -rf *.o .*.o .*.o.d core .depend .*.cmd \
		*.ko *.mod.c .tmp_versions *.mod modules.order *.symvers

define JQ_FILTER
[ split("\n")[] | select(. != "") | { \
	file: ., \
	directory: "$(SRCDIR)", \
	command: "$(CC) -I$(KERNELDIR)/include -I$(KERNELDIR)/include/generated $(_ARCH_INCLUDE) -o \(.).o \(.) " \
}]
endef

compile_commands:
	@find src -name \*.c -printf "%f\n" | \
		jq -sMR '$(JQ_FILTER)' > compile_commands.json

.PHONY: modules modules_install clean compile_commands

all: modules

else
    # called from kernel build system: just declare what our modules are
    obj-m := hd44780_lcd.o
endif