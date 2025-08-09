#!/bin/bash

# Synchronize package list and install necessary packages
sudo apt-get update && sudo apt-get install dkms linux-headers-$(uname -r)

# Rename kernel header for kernel version 6.12 or higher
KERNEL_MAJOR=$(uname -r | cut -d. -f1)
KERNEL_MINOR=$(uname -r | cut -d. -f2)
if [[ $KERNEL_MAJOR -gt 6 ]] || [[ $KERNEL_MAJOR -eq 6 && $KERNEL_MINOR -ge 12 ]]; then
    sed -i 's#<asm/unaligned.h>#<linux/unaligned.h>#' focal_spi.c
fi

# Update Makefile for being built after kernel upgrades
sed -i 's/KERNEL_VERSION :=/KERNEL_VERSION ?=/' Makefile
sed -i 's/KERNELDIR :=/KERNELDIR ?=/' Makefile

# Find the latest version
latest_spi=$(grep -E '^#define[[:space:]]+VERSION' focal_spi.c | sed -E 's/.*"([^"]*)".*/\1/' \
| tr -cd '0-9.')

# Create DKMS source directory
sudo mkdir -p /usr/src/focaltech-spi-dkms-$latest_spi

# Copy source files and add DKMS configuration
sudo cp focal_spi.c /usr/src/focaltech-spi-dkms-$latest_spi/
sudo cp Makefile /usr/src/focaltech-spi-dkms-$latest_spi/
sudo tee /usr/src/focaltech-spi-dkms-$latest_spi/dkms.conf << EOF
PACKAGE_NAME="focaltech-spi-dkms"
PACKAGE_VERSION="$latest_spi"
BUILT_MODULE_NAME[0]="focal_spi"
DEST_MODULE_LOCATION[0]="/kernel/drivers/spi"
AUTOINSTALL="yes"
MAKE[0]="make KERNELDIR=\\\${kernel_source_dir} \
-C \\\${dkms_tree}/\\\${PACKAGE_NAME}/\\\${PACKAGE_VERSION}/build"
EOF

# Set the correct ownership and permissions
sudo chown -R root:root /usr/src/focaltech-spi-dkms-$latest_spi
sudo chmod -R 644 /usr/src/focaltech-spi-dkms-$latest_spi/*
sudo chmod 755 /usr/src/focaltech-spi-dkms-$latest_spi

# Add, build and install the module
sudo dkms add -m focaltech-spi-dkms -v $latest_spi
sudo dkms build -m focaltech-spi-dkms -v $latest_spi
sudo dkms install -m focaltech-spi-dkms -v $latest_spi

# Start the module
sudo modprobe focal_spi
