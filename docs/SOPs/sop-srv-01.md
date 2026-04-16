# SOP-SRV-01: Host Server Provisioning - iSCSI Target & PXE Infrastructure

**Document Control:**
* **Scope:** Preparation and teardown of the Arch Linux host server to provide block storage and chainloaded PXE services. 
* **Out of Scope:** Client hardware execution, UEFI module compilation, payload generation. 
* **Target Host OS:** Arch Linux (x86_64)
* **Date Verified:** April 16, 2026
* **Status:** Validated
* **Related Documents:**
  * [SOP-NET-02: Subnet Isolation Protocols](./sop-net-02.md)

---

### 1. Storage Provisioning (Sparse Block Allocation)
**Objective:** Allocate virtualized block storage without modifying physical partition boundaries.

**Execution:**
```bash
# 1.1 Create target directory
sudo mkdir -p /var/lib/iscsi_disks

# 1.2 Provision a 64GB sparse block image (allocates near-zero physical bytes)
sudo truncate -s 64G /var/lib/iscsi_disks/win-san.img

# 1.3 Verify allocation and apparent size
ls -lh /var/lib/iscsi_disks/win-san.img
```

---

### 2. iSCSI Target Configuration (LIO Kernel Target)
**Objective:** Expose the sparse block image as an unauthenticated iSCSI LUN to the local research subnet.

**Execution:**
```bash
# 2.1 Install administration shell and initialize service
sudo pacman -S targetcli-fb --noconfirm
sudo systemctl enable --now target

# 2.2 Enter target administration shell
sudo targetcli
```

*Execute the following mapping instructions sequentially within the `/>` prompt:*
```text
cd /backstores/fileio
create win_disk0 /var/lib/iscsi_disks/win-san.img 64G false

cd /iscsi
create iqn.2026-04.local.arch:win-target

cd /iscsi/iqn.2026-04.local.arch:win-target/tpg1/luns
create /backstores/fileio/win_disk0

cd ../portals
create 0.0.0.0 3260

cd ../
set attribute authentication=0 demo_mode_write_protect=0 generate_node_acls=1 cache_dynamic_acls=1

saveconfig
exit
```

---

### 3. Network Services Isolation (dnsmasq)
**Objective:** Deploy DHCP and TFTP services explicitly isolated from the host's primary DNS resolver to prevent system state contamination.

**Execution:**
```bash
# 3.1 Install service and prepare TFTP root
sudo pacman -S dnsmasq --noconfirm
sudo mkdir -p /srv/tftp

# 3.2 Backup default configuration
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.bak

# 3.3 Construct custom ruleset
sudo nano /etc/dnsmasq.conf
```

*Input the configuration below. Replace variables in brackets `[]` with host environment parameters.*

```ini
# /etc/dnsmasq.conf
# ----------------------------------------------------
# A. SERVICE ISOLATION
# ----------------------------------------------------
# Disable DNS module entirely (prevents systemd-resolved conflicts)
port=0
# Bind strictly to the isolated interface (optional but recommended)
# interface=[INTERFACE_NAME]
# bind-interfaces


# ----------------------------------------------------
# B. DHCP SCOPE
# ----------------------------------------------------
# Format: [Start IP],[End IP],[Lease Time]
dhcp-range=10.0.0.100,10.0.0.150,12h
dhcp-option=3,10.0.0.1              # Default Gateway (This Host)
dhcp-option=6,1.1.1.1               # Upstream DNS

# ----------------------------------------------------
# C. TFTP & PXE CHAINLOADING
# ----------------------------------------------------
enable-tftp
tftp-root=/srv/tftp

# Tag native firmware requests and serve iPXE binary
dhcp-userclass=set:ipxe,iPXE
dhcp-boot=tag:!ipxe,ipxe.efi

# Tag iPXE requests and hand off to execution script
dhcp-boot=tag:ipxe,boot.ipxe
```

```bash
# 3.4 Initialize network services
sudo systemctl enable --now dnsmasq
```

---

### 4. Bootloader Staging
**Objective:** Stage the chainload payloads in the TFTP root for client retrieval.

**Execution:**
```bash
# 4.1 Retrieve upstream UEFI binary
sudo curl -o /srv/tftp/ipxe.efi https://boot.ipxe.org/ipxe.efi

# 4.2 Construct the SAN execution script
sudo nano /srv/tftp/boot.ipxe
```

*Input the script below. Replace `[ARCH_IP]` with the static IP of this host server (Default: `10.0.0.1` for Direct Link).*

```text
#!ipxe
echo =========================================
echo Initiating iSCSI SAN Boot Sequence
echo Host: SOP-SRV-01
echo =========================================

# Hook target to RAM as Drive 0x80
sanhook --drive 0x80 iscsi:[ARCH_IP]::::iqn.2026-04.local.arch:win-target || goto failed

# Boot attached device
sanboot --drive 0x80 || goto failed

:failed
echo SAN Boot Failed. Dropping to debug shell.
shell
```

**Completion State:** The host is now actively broadcasting PXE capabilities and serving the 64GB LUN.

---

### 5. Infrastructure Teardown
**Objective:** Revert the host server to its baseline state, purging all target configurations and experimental storage blocks.

**Execution:**
```bash
# 5.1 Terminate active services
sudo systemctl disable --now dnsmasq target

# 5.2 Purge network configurations and TFTP directory
sudo rm /etc/dnsmasq.conf
sudo mv /etc/dnsmasq.conf.bak /etc/dnsmasq.conf
sudo rm -rf /srv/tftp

# 5.3 Erase kernel target configurations
sudo targetcli clearconfig

# 5.4 Destroy storage block
sudo rm -rf /var/lib/iscsi_disks
```
