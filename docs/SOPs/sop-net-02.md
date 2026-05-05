# SOP-NET-02: Direct Link Infrastructure & NIC Stabilization

**Document Control**

| Field | Value |
|---|---|
| Scope | Point-to-point Ethernet setup, Realtek driver stabilization, static IP provisioning (`10.0.0.1`) |
| Prerequisite | Server and client physically connected via Cat5e/Cat6 cable (no switch) |
| Host OS | Alpine Linux (x86_64) |
| Date Verified | May 2026 |
| Status | Validated |
| Related | [SOP-SRV-01: iSCSI Target & PXE Provisioning](./sop-srv-01.md), [SOP-NET-03: r8168 Local Package Build](./sop-net-03.md) |

---

## 1. Interface Identification

Find the NIC name for the direct-link connection and export it for use throughout this SOP.

```bash
ip link show

# Set for current session — replace eth0 with your actual interface name
export TARGET_IF="eth0"
```

---

## 2. Realtek Driver Stabilization (r8169 → r8168)

The default `r8169` kernel module causes link flapping under sustained iSCSI throughput due to TCP offload issues. The proprietary `r8168` module is stable for this workload.

```bash
# 2.1 Attempt direct install from Alpine community repo
doas apk add r8168
```

If the package is not found, build and install it locally first — follow **[SOP-NET-03: r8168 Local Package Build](./sop-net-03.md)**, then return here.

```bash
# 2.2 Blacklist r8169 (skip if installed via SOP-NET-03 — blacklist is bundled)
echo "blacklist r8169" | doas tee /etc/modprobe.d/r8168-blacklist.conf

# 2.3 Swap the active module
doas modprobe -r r8169
doas modprobe r8168

# 2.4 Confirm the correct driver is in use
lspci -vnn | grep -A 10 Ethernet | grep "Kernel driver in use"
# Expected: Kernel driver in use: r8168
```

---

## 3. Power-State Hardening (ASPM & EEE)

PCIe ASPM and Energy Efficient Ethernet (EEE) will drop active iSCSI sessions during idle micro-bursts. Disable both permanently.

```bash
# 3.1 Install ethtool
doas apk add ethtool

# 3.2 Disable EEE on the physical port
doas ethtool --set-eee $TARGET_IF eee off

# 3.3 Disable Wake-on-LAN
doas ethtool -s $TARGET_IF wol d

# 3.4 Disable PCIe ASPM via kernel boot parameter
doas nano /etc/default/grub
```

Locate `GRUB_CMDLINE_LINUX_DEFAULT` and append `pcie_aspm=off`. Example:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet pcie_aspm=off"
```

```bash
# 3.5 Regenerate GRUB config
doas grub-mkconfig -o /boot/grub/grub.cfg
```

> **Note:** The ASPM change requires a reboot to take effect. Reboot after completing Section 4.

---

## 4. Static IP Provisioning (10.0.0.1)

Assign a static IP to the direct-link interface using Alpine's native `/etc/network/interfaces`. This is simpler and more reliable than NetworkManager or networkd on Alpine.

```bash
doas nano /etc/network/interfaces
```

Add the following block. Replace `eth0` with your `$TARGET_IF`.

```
auto eth0
iface eth0 inet static
    address 10.0.0.1
    netmask 255.255.255.0
```

```bash
# Apply the new configuration
doas rc-service networking restart

# Verify
ip addr show $TARGET_IF
```

Expected: `inet 10.0.0.1/24` on the interface.

---

## 5. dnsmasq Subnet Update

If you provisioned dnsmasq in SOP-SRV-01 before running this SOP, update the config to bind explicitly to the direct-link interface.

```bash
doas nano /etc/dnsmasq.conf
```

Ensure these two lines are set correctly (should already match if SOP-SRV-01 was followed after this document):

```ini
interface=eth0       # Replace with your $TARGET_IF
bind-interfaces
```

```bash
doas rc-service dnsmasq restart
```

---

## 6. Teardown & Rollback

```bash
# 6.1 Restore the default r8169 driver
doas modprobe -r r8168
doas modprobe r8169

# 6.2 Remove the r8168 package
# If installed from the Alpine repo:
doas apk del r8168
# If installed via SOP-NET-03 (local build), also remove the blacklist config:
doas rm -f /etc/modprobe.d/r8168-blacklist.conf

# 6.3 Remove the static IP config
doas nano /etc/network/interfaces
# Delete or comment out the eth0 static block added in Section 4
doas rc-service networking restart

# 6.4 Remove the ASPM kernel parameter
doas nano /etc/default/grub
# Remove pcie_aspm=off from GRUB_CMDLINE_LINUX_DEFAULT
doas grub-mkconfig -o /boot/grub/grub.cfg
```
