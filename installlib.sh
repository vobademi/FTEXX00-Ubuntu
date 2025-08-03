#!/bin/bash

# Create working directory and unpack debian package
mkdir debdir
dpkg-deb -R libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb debdir

# Use override.conf instead of fprintd.service to prevent conflicts
rm -r ./debdir/usr/lib/systemd
mkdir -p ./debdir/etc/systemd/system/fprintd.service.d
cat << 'EOF' > ./debdir/etc/systemd/system/fprintd.service.d/override.conf
[Service]
DeviceAllow=/dev/focal_moh_spi rw
EOF

# Append custom1 to the package name
sed -i 's/\(Version: 1:1.94.4+tod1-0ubuntu1~22.04.2\)/\1+custom1/' ./debdir/DEBIAN/control

# Update md5sums
cd debdir/
find . -type f -not -path "./DEBIAN/*" -exec md5sum {} + | sort -k 2 | sed 's/\.\/\(.*\)/\1/' > DEBIAN/md5sums
cd ..

# Repack the modified debian package
dpkg-deb -b --root-owner-group debdir libfprint-2-2+custom1.deb
rm -r debdir

# Synchronize package list and install necessary dependencies
sudo apt update
sudo apt install fprintd fprintd-doc libpam-fprintd -y

# Install the modified debian package
sudo dpkg -i --force-overwrite libfprint-2-2+custom1.deb
