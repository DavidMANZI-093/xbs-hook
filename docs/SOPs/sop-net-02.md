# SOP-NET-02: Direct Link Infrastructure & NIC Stabilization

**Document Control:**
* **Scope:** Configuration of a direct point-to-point Ethernet topology, including Realtek hardware stabilization, static IP provisioning (`10.0.0.1`), and DHCP scope realignment.
* **Prerequisite:** Server and Client hardware physically connected via a direct Cat5e/Cat6 Ethernet cable (no intermediary switch).
* **Target Host OS:** Arch Linux (x86_64)
* **Date Verified:** April 16, 2026
* **Status:** Validated
* **Related Documents:** [SOP-SRV-01: Host Server Provisioning](./sop-srv-01.md)

---

### 1. Hardware Initialization & Interface Identification
**Objective:** Identify the physical network interface card (NIC) connecting the host to the client.

**Execution:**
```bash
# Identify the Ethernet interface name (e.g., enp3s0, eno1)
ip link show

# Define environment variable for current session (Replace enp3s0 with your actual interface)
export TARGET_IF="enp3s0"
```

---

### 2. NIC Driver Stabilization (Realtek r8169 to r8168)
**Rationale:** The open-source `r8169` kernel module inherently struggles with hardware TCP offloading and high-throughput block data over iSCSI, leading to link-flapping. Downgrading to the proprietary `r8168` module resolves buffer saturation.

**Execution:**
```bash
# 2.1 Install the proprietary Realtek driver (requires linux-headers)
sudo pacman -S r8168-dkms linux-headers --noconfirm

# 2.2 Blacklist the unstable open-source module
echo "blacklist r8169" | sudo tee /etc/modprobe.d/r8169_blacklist.conf

# 2.3 Unload current module and load the proprietary driver
sudo rmmod r8169
sudo modprobe r8168

# 2.4 Verify the active driver is r8168
lspci -vnn | grep -A 10 Ethernet | grep "Kernel driver in use"
```

---

### 3. Power-State Override (ASPM & EEE Hardening)
**Rationale:** Realtek controllers drop active iSCSI sessions when PCIe Active State Power Management (ASPM) or Energy Efficient Ethernet (EEE) attempts to put the bus/link to sleep between micro-bursts of packet transfers. These must be aggressively disabled.

**Execution:**
```bash
# 3.1 Install ethtool for hardware-level interface manipulation
sudo pacman -S ethtool --noconfirm

# 3.2 Disable Energy Efficient Ethernet (EEE) on the physical port
sudo ethtool --set-eee $TARGET_IF eee off

# 3.3 Disable Wake-on-LAN (WoL) to prevent magic-packet interrupts
sudo ethtool -s $TARGET_IF wol d

# 3.4 Disable PCIe ASPM at the kernel level (Persistent Fix)
sudo nano /etc/default/grub
```
*Locate `GRUB_CMDLINE_LINUX_DEFAULT` and append `pcie_aspm=off`. Example:*
`GRUB_CMDLINE_LINUX_DEFAULT="loglevel=3 quiet pcie_aspm=off"`

```bash
# 3.5 Regenerate the GRUB boot configuration to apply the ASPM override
sudo grub-mkconfig -o /boot/grub/grub.cfg
```
*(Note: The ASPM override requires a system reboot to take effect on the PCIe bus. Execute this reboot after completing Section 4).*

---

### 4. Static Link Provisioning (10.0.0.1)
**Rationale:** Establish an isolated `/24` subnet on the direct link. Utilizing `systemd-networkd` prevents desktop tools (like NetworkManager) from dynamically altering the IP state during the iSCSI handoff.

**Execution:**
```bash
# 4.1 Create a dedicated network profile
sudo nano /etc/systemd/network/10-direct-link.network
```

*Input the following configuration. Ensure the `Name=` field matches your `$TARGET_IF`.*
```ini
[Match]
Name=enp3s0

[Network]
Address=10.0.0.1/24
# Disables link-local addressing and IPv6 to enforce strict IPv4 routing
LinkLocalAddressing=no
IPv6AcceptRA=no

[Link]
# Force link layer to remain active
RequiredForOnline=yes
```

```bash
# 4.2 Restrict NetworkManager from interfering with this specific interface
sudo nano /etc/NetworkManager/conf.d/99-unmanaged-devices.conf
```
*Input the following to enforce isolation:*
```ini
[keyfile]
unmanaged-devices=interface-name:enp3s0
```

```bash
# 4.3 Restart networking services to apply static provisioning
sudo systemctl restart NetworkManager
sudo systemctl enable --now systemd-networkd
sudo systemctl restart systemd-networkd
```

---

### 5. DHCP & PXE Subnet Reconfiguration (dnsmasq)
**Rationale:** Update the `dnsmasq` architecture provisioned in [SOP-SRV-01](./sop-srv-01.md) to serve the new `10.0.0.X` scope exclusively to the direct link interface.

**Execution:**
```bash
# 5.1 Rebuild dnsmasq configuration
sudo nano /etc/dnsmasq.conf
```

*Input the updated architecture below:*

```ini
# /etc/dnsmasq.conf
# ----------------------------------------------------
# A. SERVICE ISOLATION
# ----------------------------------------------------
port=0
# Bind strictly to the direct link interface to prevent network bridging loops
interface=enp3s0
bind-interfaces

# ----------------------------------------------------
# B. DHCP SCOPE (10.0.0.X)
# ----------------------------------------------------
# Provide IP addresses to the client machine
dhcp-range=10.0.0.100,10.0.0.150,12h
dhcp-option=3,10.0.0.1        # Gateway (This Arch Server)
dhcp-option=6,1.1.1.1         # DNS (Not strictly required for bare-metal SAN)

# ----------------------------------------------------
# C. TFTP & PXE CHAINLOADING
# ----------------------------------------------------
enable-tftp
tftp-root=/srv/tftp
dhcp-userclass=set:ipxe,iPXE
dhcp-boot=tag:!ipxe,ipxe.efi
dhcp-boot=tag:ipxe,boot.ipxe
```

```bash
# 5.2 Restart the DHCP service to broadcast the new scope
sudo systemctl restart dnsmasq
```

**Completion State:** The network infrastructure is now point-to-point, heavily stabilized against power interruptions, and operating on the `10.0.0.0/24` subnet. Ensure `boot.ipxe` from [SOP-SRV-01](./sop-srv-01.md) is updated so the iSCSI target IP reads `10.0.0.1`. **Reboot the host** to apply the `pcie_aspm=off` kernel parameter before initiating client boot.

---

### 6. Infrastructure Teardown & Rollback
**Objective:** Reverse the network state to restore native Arch Linux connectivity.

**Execution:**
```bash
# 1. Rollback Realtek Drivers
sudo rm /etc/modprobe.d/r8169_blacklist.conf
sudo rmmod r8168
sudo modprobe r8169
sudo pacman -Rns r8168-dkms

# 2. Remove Network Overrides
sudo rm /etc/systemd/network/10-direct-link.network
sudo rm /etc/NetworkManager/conf.d/99-unmanaged-devices.conf
sudo systemctl restart NetworkManager

# 3. Kernel Parameter Cleanup
# (Manually edit /etc/default/grub to remove pcie_aspm=off, then run:)
sudo grub-mkconfig -o /boot/grub/grub.cfg
```
