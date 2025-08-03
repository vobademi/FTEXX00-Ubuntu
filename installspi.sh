#!/bin/bash

# Update packages and install necessary packages
sudo apt update
sudo apt install dkms linux-headers-$(uname -r)

# Rename kernel header for kernel version 6.12 or higher
KERNEL_MAJOR=$(uname -r | cut -d. -f1)
KERNEL_MINOR=$(uname -r | cut -d. -f2)
if [[ $KERNEL_MAJOR -gt 6 ]] || [[ $KERNEL_MAJOR -eq 6 && $KERNEL_MINOR -ge 12 ]]; then
    sed -i 's#<asm/unaligned.h>#<linux/unaligned.h>#' focal_spi.c
fi

# Update Makefile for being built after kernel upgrades
sed -i 's/KERNEL_VERSION :=/KERNEL_VERSION ?=/' Makefile
sed -i 's/KERNELDIR :=/KERNELDIR ?=/' Makefile

# Create DKMS source directory
sudo mkdir -p /usr/src/focal-spi-1.0.3

# Copy source files and add DKMS configuration
sudo cp focal_spi.c /usr/src/focal-spi-1.0.3/
sudo cp Makefile /usr/src/focal-spi-1.0.3/
sudo tee /usr/src/focal-spi-1.0.3/dkms.conf << 'EOF'
PACKAGE_NAME="focal-spi"
PACKAGE_VERSION="1.0.3"
BUILT_MODULE_NAME[0]="focal_spi"
DEST_MODULE_LOCATION[0]="/kernel/drivers/spi"
AUTOINSTALL="yes"
MAKE[0]="make KERNELDIR=${kernel_source_dir} -C ${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build"
EOF

# Set the correct ownership and permissions
sudo chown -R root:root /usr/src/focal-spi-1.0.3
sudo chmod -R 644 /usr/src/focal-spi-1.0.3/*
sudo chmod 755 /usr/src/focal-spi-1.0.3

# Add, build and install the module
sudo dkms add -m focal-spi -v 1.0.3
sudo dkms build -m focal-spi -v 1.0.3
sudo dkms install -m focal-spi -v 1.0.3

# Start the module
sudo modprobe focal_spi
