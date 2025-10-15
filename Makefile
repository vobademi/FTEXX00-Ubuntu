KERNEL_VERSION := $(shell uname -r)
KERNELDIR := /lib/modules/$(KERNEL_VERSION)/build
CURRENT_PATH := $(shell pwd)
OBJ_NAME := focal_spi

obj-m   += $(OBJ_NAME).o

build: kernel_modules

kernel_modules:
		$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules

install: kernel_modules
		install -p -D -m 0755 $(OBJ_NAME).ko  /lib/modules/$(KERNEL_VERSION)/kernel/drivers/spi/$(OBJ_NAME).ko
		depmod
		modprobe $(OBJ_NAME)
				
clean:
		$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean


       
