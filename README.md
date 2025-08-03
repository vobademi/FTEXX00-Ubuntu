# FTEXX00-Ubuntu

`installspi.sh` and `installlib.sh` are bash scripts to install the SPI module and the proprietary libfprint driver for FTE3600, FTE4800, FTE6600 and FTE6900 fingerprint readers on Ubuntu 24.04.2 LTS (officially supported) and other Debian-based distros.

> ### ⚠ Warning
>
> Debian Stable *bookworm* is not supported. See [Troubleshooting](#troubleshooting) for more details.

### Table of Contents

- [Introduction](#ftexx00-ubuntu)
- [Installation](#installation)
- [Troubleshooting](#troubleshooting)
- [Updating and Uninstalling](#updating-and-Uninstalling)
- [Questions](#questions)
- [Copying](#copying)

### installspi.sh

`installspi.sh` installs the SPI module as [DKMS](https://en.wikipedia.org/wiki/Dynamic_Kernel_Module_Support) (Dynamic Kernel Module Support) to preserve the module across kernel upgrades. The following changes are made to install the module properly:

+ The script determines your kernel version. If it's 6.12 or higher, it renames the header from `asm/unaligned.h` to `linux/unaligned.h` in `focal_spi.c` to compile the driver without fatal exception error.
+ The script updates `Makefile` by modifying `KERNEL_VERSION :=` and `KERNELDIR :=` by changing `:` to `?` to allow being rebuilt after kernel upgrades.

### installlib.sh

`installlib.sh` unpacks, modifies, repacks, and installs the latest libfprint package. These modifications are made because the official package has several issues:

+ The official package attempts to overwrite `fprintd.service` in `/usr/lib/systemd/system` directory. A package overwriting another packages' file(s) are discouraged since it'll cause several problems.
    + To fix this, the script deletes the conflicting file and adds `override.conf` which includes the changes from `fprintd.service` to `/debdir/etc/systemd/system/fprintd.service.d` directory. This is the recommended way to add override configurations by systemd.
+ The official package contains wrong *md5sums*. If the *md5sums* inside a Debian package is wrong, the package manager might detect a checksum mismatch and refuse to install the package.
    + To fix this, the scripts updates *md5sums* to ensure integrity checks pass.

## Installation

1. Clone [*ubuntu_spi* repository](https://github.com/ftfpteams/ubuntu_spi):
```bash
git clone https://github.com/ftfpteams/ubuntu_spi.git
```

> 🛈 Note: This repository currently targets the following commit from *ubuntu_spi*: **d534b7a**
>
> If this repository falls behind *ubuntu_spi*, you can force `git` to return back to the target commit by:
>
> ```bash
>
> cd ubuntu_spi
>
> git reset --hard d534b7a
>
> ```

2. Copy `installspi.sh` and `installlib.sh` into the repository's root directory. The directory tree should look like this:
```bash
./ubuntu_spi
├── focal_driver_open_test
├── focal_spi.c
├── installlib.sh
├── installspi.sh
├── libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb
├── libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_amd64_20240620.deb
├── Makefile
└── README.md
```

3. Make the scripts executable:
```bash
chmod +x installlib.sh installspi.sh
```

4. Install the SPI Module:
```bash
./installspi.sh
```

&nbsp;&nbsp;&nbsp;↳ ***Advanced configuration for UEFI Secure Boot (Skip to step 5 if Secure Boot disabled)***

If you have Secure Boot enabled on your PC, you might see this line after running `installspi.sh`:

```bash
modprobe: ERROR: could not insert 'focal_spi': Key was rejected by service
```

This means that you need to enroll a signing key to make the module trusted by Secure Boot.

You might see a prompt *Configuring Secure Boot* for the first time:

4a. Scroll down and select `Ok`. Enter a password for Secure Boot.

> ✓ Tip: If you didn't see a prompt *Configuring Secure Boot*, you can enter:
>
> ```bash
> sudo mokutil --import /var/lib/shim-signed/mok/MOK.der
> ```
>
> and choose a password.

4b. Reboot. Upon system reboot, you will be greeted with *Shin UEFI key management*. Press any key to perform MOK management.

4c. Choose `Enroll MOK`, `Continue`, `Yes` and enter the password you've chosen earlier. Finally, `Reboot`.

5. Install libfprint:
```bash
./installlib.sh
```

6. GNOME and GDM have native support for fprint, so you don't need additional configuration on Ubuntu. Go to **Settings > System > Users > Fingerprint Login** and enroll your fingerprint.

## Troubleshooting

### My PC stuck at login screen after installing libfprint

This may occur on distros like Debian Stable *bookworm* that ship outdated packages. libfprint conflicts with some of these packages, possibly GDM, and prevents from logging in. To recover your PC:

1. Boot into a live environment (USB installation media).

2. Gain root privileges:
```bash
sudo su
```

3. Use fdisk tool to list drives:
```bash
fdisk -l
```
Identify your partition that has your operating system installed by locating `Linux` or `Linux filesystem`. In this example, the drive is /dev/nvme0n1 and the partition that has the operating system installed is 2. So, the partition is **/dev/nvme0n1p2**:
```bash
Disk /dev/nvme0n1: 476.94 GiB, 512110190592 bytes, 1000215216 sectors
Disk model: SAMSUNG MZVL2512HCJQ-00B00
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / 512 bytes
Disklabel type: gpt
Disk identifier: 6CE694C6-8CE1-41E1-98DA-677436595609

Device           Start        End   Sectors   Size Type
/dev/nvme0n1p1    2048    2203647   2201600     1G EFI System
/dev/nvme0n1p2 2203648 1000212479 998008832 475.9G Linux filesystem
```

4. Mount the partition. Replace *partition* with the partition you've identified such as **nvme0n1p2**:
```bash
mount /dev/partition /mnt
```

5. Change root:
```bash
chroot /mnt
```

6. Uninstall libfprint:
```bash
apt remove libfprint-2-2
```

7. Reboot.

## Updating and Uninstalling

To update, uninstall and reinstall. If only one of them received an update, you don't have to uninstall the one that didn't receive an update.

### Uninstall the SPI module

1. Gain root privileges:
```bash
sudo su
```

2. Unload the module:
```bash
modprobe -r focal_spi
```

3. Remove from DKMS:
```bash
dkms remove -m focal-spi -v 1.0.3 --all
```

4. Remove source directory:
```bash
rm -rf /usr/src/focal-spi-1.0.3
```

### Uninstall libfprint

```bash
sudo apt remove libfprint-2-2
```

## Questions

### Why didn't you fork *ubuntu_spi* repository, publish the scripts alongside, or modify the files and serve them to us?

Since *ftfpteams* did not add a license to their repository, their work is protected by copyright. Distributing or modifying their software without permission could potentially violate copyright law.  
*(Clarification needed, I'm not a lawyer)*

## Copying

[![CC0 1.0 Universal](https://mirrors.creativecommons.org/presskit/buttons/88x31/svg/cc-zero.svg)](https://creativecommons.org/publicdomain/zero/1.0/)

[↑ Go back to top](#ftexx00-ubuntu)
