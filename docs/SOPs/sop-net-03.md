# SOP-NET-03: r8168 Driver — Local Package Build & Install

**Document Control**

| Field | Value |
|---|---|
| Scope | Build the `r8168` Realtek driver as a local Alpine package and install it on the iSCSI server |
| Prerequisite | Running Alpine machine with `linux-lts` installed |
| Host OS | Alpine Linux (x86_64) |
| Date Verified | May 2026 |
| Status | Validated |
| Related | [SOP-NET-02: Direct Link & NIC Stabilization](./sop-net-02.md) |

---

## 1. Build Environment

```bash
# Install build dependencies
doas apk add alpine-sdk linux-lts-dev build-base

# Verify kernel headers are reachable at the expected path
# This should resolve correctly when linux-lts and linux-lts-dev are in sync
ls /lib/modules/$(uname -r)/build
```

> **Note:** If headers are missing, the most likely cause is a kernel/headers version mismatch — i.e., `linux-lts` and `linux-lts-dev` are at different versions. Run `doas apk upgrade linux-lts linux-lts-dev` to synchronize them, then reboot before building.

```bash
# Create an unprivileged build user (abuild blocks root builds)
doas adduser -D builder
doas addgroup builder abuild

# Fix distfiles cache ownership so builder can download sources
doas chown -R builder:abuild /var/cache/distfiles

# Switch to builder — adduser -D sets no password, use doas -u instead of su
doas -u builder -s

# Generate the signing key only — do not use -i (requires root)
abuild-keygen -a

# Exit back to your main user and install the key manually
exit
doas cp /home/builder/.abuild/*.rsa.pub /etc/apk/keys/
```

---

## 2. APKBUILD

```bash
# Create the package directory as builder to ensure correct ownership
# abuild derives the repo name from the PARENT of the APKBUILD directory:
#   output = ~/packages/<parent-name>/x86_64/
# So to get ~/packages/r8168/x86_64/, the structure must be ~/r8168/r8168/
doas -u builder -s
mkdir -p ~/r8168/r8168
cd ~/r8168/r8168
```

Create `APKBUILD`:

```
# Maintainer: Your Name <your@email.com>
pkgname=r8168
pkgver=8.056.02
pkgrel=1
pkgdesc="Realtek r8168 ethernet driver"
url="https://github.com/mtorromeo/r8168"
arch="all"
license="GPL"
depends="linux-lts"
makedepends="linux-lts-dev build-base"
source="$pkgname-$pkgver.tar.gz::https://github.com/mtorromeo/r8168/archive/refs/tags/$pkgver.tar.gz"
_kernelver=$(uname -r)

build() {
    cd "$builddir"
    make modules -C /lib/modules/$_kernelver/build M="$builddir/src"
}

package() {
    cd "$builddir"

    install -D -m644 src/r8168.ko \
        "$pkgdir"/lib/modules/$_kernelver/kernel/drivers/net/ethernet/realtek/r8168.ko

    mkdir -p "$pkgdir"/etc/modprobe.d
    echo "blacklist r8169" > "$pkgdir"/etc/modprobe.d/r8168-blacklist.conf
}

sha512sums="SKIP"
```

```bash
# Download source, generate checksums, and build
abuild checksum
abuild -rd
```

The `.apk` lands in `~/packages/r8168/x86_64/`.

---

## 3. Install Locally

```bash
# Exit builder shell back to your main user first
exit

# Install directly from the built file — no repo needed
doas apk add --allow-untrusted /home/builder/packages/r8168/x86_64/r8168-*.apk
```

Load the driver and verify:

```bash
doas modprobe -r r8169
doas modprobe r8168

lspci -vnn | grep -A 10 Ethernet | grep "Kernel driver in use"
# Expected: Kernel driver in use: r8168
```

Return to [SOP-NET-02 Section 2](./sop-net-02.md) to continue.

---

## 4. Kernel Update Procedure

When `linux-lts` is upgraded the existing `r8168.ko` will be built against stale headers and fail to load, dropping the iSCSI link. Rebuild before rebooting.

```bash
# 4.1 Upgrade the kernel (do not reboot yet)
doas apk upgrade linux-lts linux-lts-dev

# 4.2 Switch to the builder user and go to the package directory
doas -u builder -s
cd ~/r8168/r8168

# 4.3 Bump pkgrel in APKBUILD to invalidate the old build
# Change: pkgrel=1 → pkgrel=2 (increment on each rebuild)
nano APKBUILD

# 4.4 Rebuild against the new headers
abuild checksum
abuild -rd

# 4.5 Exit builder shell and install as main user
exit
doas apk add --allow-untrusted /home/builder/packages/r8168/x86_64/r8168-*.apk

# 4.6 Reboot to load the new kernel and driver together
doas reboot
```

After reboot, verify the driver loaded correctly:

```bash
lspci -vnn | grep -A 10 Ethernet | grep "Kernel driver in use"
# Expected: Kernel driver in use: r8168
```

---

## Transferring the Package to Another Machine (if needed)

From the build machine, serve the file temporarily:

```bash
cd ~/packages/r8168/x86_64/
python3 -m http.server 8080
```

On the target machine:

```bash
wget http://[BUILD_MACHINE_IP]:8080/r8168-8.056.02-r1.x86_64.apk
doas apk add --allow-untrusted r8168-8.056.02-r1.x86_64.apk
```

---

## 6. Teardown

Undo the driver build and installation. Pairs with the Teardown section in [SOP-NET-02](./sop-net-02.md).

```bash
# 6.1 Remove the blacklist and restore the default r8169 driver
doas modprobe -r r8168
doas modprobe r8169

# 6.2 Remove the installed package
doas apk del r8168

# 6.3 Remove the blacklist config (if it was not bundled in the package)
doas rm -f /etc/modprobe.d/r8168-blacklist.conf

# 6.4 Optionally remove the builder user and build tree
doas deluser --remove-home builder
```

Verify the default driver is active again:

```bash
lspci -vnn | grep -A 10 Ethernet | grep "Kernel driver in use"
# Expected: Kernel driver in use: r8169
```
