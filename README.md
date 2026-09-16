# FTEXX00-Ubuntu

> [!IMPORTANT]
> ## See work in progress open source driver for FTE3600 at https://github.com/SamSeven777/libfprint-fte3600

> [!IMPORTANT]
> [ubuntu_spi](https://github.com/ftfpteams/ubuntu_spi) has been taken down following a DMCA notice. The takedown was issued because *ftfpteams* did not disclose the source code for the libfprint package.

`installspi.sh` and `installlib.sh` are bash scripts to install the SPI module and the ~~proprietary~~ libfprint driver for FTE3600, FTE4800, FTE6600 and FTE6900 fingerprint readers on Ubuntu 24.04 LTS (officially supported) and other Debian-based distros.

Debian *bookworm* and below are **not** supported. See [troubleshooting](#troubleshooting) for Debian *trixie* specific fixes.

## Table of Contents

- [Introduction](#introduction)
- [Installation](#installation)
- [Troubleshooting](#troubleshooting)
- [Uninstalling](#uninstalling)
- [Questions](#questions)
- [Copying](#copying)

## Introduction

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

### alt/focal_spi.c (optional)

`alt/focal_spi.c` is an alternative code for the SPI module, sent by @ctfdavis due to some devices give `init sensor error!` when the official code is used. See [troubleshooting](#troubleshooting) if you see `init sensor error!` after the installation.

## Installation

1. Clone this repository:
```bash
git clone https://github.com/vobademi/FTEXX00-Ubuntu.git
```

2. Copy `libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb` into the repository's root directory. [(?)](https://github.com/oneXfive/ubuntu_spi/blob/main/libfprint-2-2_1.94.4%2Btod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb "?")  
The directory tree should look like this:
```bash
./FTEXX00-Ubuntu
├── alt
│   └── focal_spi.c
├── COPYING
├── focal_spi.c
├── installlib.sh
├── installspi.sh
├── libfprint-2-2_1.94.4+tod1-0ubuntu1~22.04.2_spi_20250112_amd64.deb
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

5. Configure for UEFI Secure Boot:

> [!TIP]
> If Secure Boot is disabled or you're reinstalling the SPI module, you can skip this section.

If you have Secure Boot enabled on your PC, you might see this line after running `installspi.sh`:

```bash
modprobe: ERROR: could not insert 'focal_spi': Key was rejected by service
```

This means that you need to enroll a signing key to make the module trusted by Secure Boot. You will see the prompt *Configuring Secure Boot* for the first time:

i. Select `Ok`. Enter a password for Secure Boot.

> [!TIP]
> If you didn't see the prompt *Configuring Secure Boot*, you can enter:
>
> ```bash
> sudo mokutil --import /var/lib/shim-signed/mok/MOK.der
> ```
>
> and choose a password.

ii. Reboot. Upon system reboot, you will be greeted with *Shin UEFI key management*. Press any key to perform MOK management.

iii. Choose `Enroll MOK`, `Continue`, `Yes` and enter the password you've chosen earlier. Finally, `Reboot`.

6. Install libfprint:
```bash
./installlib.sh
```

7. When you see the prompt *PAM configuration*, make sure **Fingerprint authentication** is ticked, and select `Ok`. You can press `Tab ↹` to go below.

8. GNOME and GDM have native support for fprint, so you don't need additional configuration on Ubuntu. Go to **Settings > System > Users > Fingerprint Login** and enroll your fingerprint.  
If you are using a distro that uses SDDM such as Kubuntu, visit [SDDM#Using_a_fingerprint_reader](https://wiki.archlinux.org/title/SDDM#Using_a_fingerprint_reader).

> [!TIP]
> Logging in using a fingerprint on GDM or SDDM does not unlock GNOME Keyring or KWallet, respectively. Using your password for the initial login might be a better option.

## Troubleshooting

### I get `init sensor error!` after installation

This was reported on several machines.

1. Uninstall the SPI module (See [uninstalling](#uninstalling)).

2. Copy `focal_spi.c` from `./alt` to the root directory (overwrite the original one).

3. Re-run `installspi.sh`.

### Debian *trixie* specific fixes

#### 1. Enrolling MOK key for UEFI Secure Boot

Using `sudo mokutil --import /var/lib/shim-signed/mok/MOK.der` will not work because Debian doesn't store MOK keys in `/var/lib/shim-signed/mok`.

1. Manually generate MOK key:

```bash
sudo dkms generate_mok
```
2. Enroll it:

```bash
sudo mokutil --import /var/lib/dkms/mok.pub
```

3. Choose a password.

4. Reboot. Upon system reboot, you will be greeted with *Shin UEFI key management*. Press any key to perform MOK management.

5. Choose `Enroll MOK`, `Continue`, `Yes` and enter the password you've chosen earlier. Finally, `Reboot`.

#### 2. Fixing package conflicts

fprintd (1.94.5-2) in *trixie* packages conflicts due to it expects libfprint-2-2 (>= 1:1.94.9). The compatible fprintd packages can be installed from launchpad.

fprintd 1.94.3-1: http://launchpadlibrarian.net/723052793/fprintd_1.94.3-1_amd64.deb  
fprintd-doc 1.94.3-1: http://launchpadlibrarian.net/723052789/fprintd-doc_1.94.3-1_all.deb  
libpam-fprintd 1.94.3-1: http://launchpadlibrarian.net/723052795/libpam-fprintd_1.94.3-1_amd64.deb

1. Install the packages:

```bash
sudo dpkg -i --force-overwrite fprintd_1.94.3-1_amd64.deb fprintd-doc_1.94.3-1_all.deb libpam-fprintd_1.94.3-1_amd64.deb
```

2. Hold the packages to prevent them from being overwritten by `apt`:

```bash
sudo apt-mark hold fprintd fprintd-doc libpam-fprintd
```

## Uninstalling

### Uninstall the SPI module

1. Stop fprintd service:
```bash
sudo systemctl stop fprintd.service
```

2. Unload the module:
```bash
sudo modprobe -r focal_spi
```

3. Remove from DKMS:
```bash
module=$(dkms status | grep 'focaltech-spi-dkms' \
| cut -d',' -f1) && sudo dkms remove "$module" --all
```

4. Remove source directory:
```bash
sudo rm -rf /usr/src/focaltech-spi-dkms-*
```

### Uninstall libfprint

1. Uninstall libfprint:
```bash
sudo apt remove libfprint-2-2
```

2. Remove the hold to allow updates from official upstream:
```bash
sudo apt-mark unhold libfprint-2-2
```

## Questions

### Why didn't you fork *ubuntu_spi* repository, publish the scripts alongside, or modify the files and serve them to us?

~~Since *ftfpteams* did not add a license to their repository, their libfprint package appears to be proprietary. Distributing or modifying their software without permission could potentially violate copyright law.~~  
*ftfpteams* did not disclose the source code for the libfprint package, which is a violation of the LGPL copyright.

## Copying

[![CC0 1.0 Universal](https://mirrors.creativecommons.org/presskit/buttons/88x31/svg/cc-zero.svg)](https://creativecommons.org/publicdomain/zero/1.0/)

[↑ Go back to top](#ftexx00-ubuntu)
