# SOP-SRV-01: iSCSI Target & PXE Server Provisioning

**Document Control**

| Field | Value |
|---|---|
| Scope | Provision and teardown of the Alpine Linux host as an iSCSI block storage and PXE chainload server |
| Out of Scope | Client hardware, UEFI module compilation, payload generation |
| Host OS | Alpine Linux (x86_64) |
| Date Verified | May 2026 |
| Status | Validated |
| Related | [SOP-NET-02: Direct Link & NIC Stabilization](./sop-net-02.md) |

---

## Prerequisites

- Alpine Linux installed and accessible via SSH or console
- Static IP assigned to the server NIC (default: `10.0.0.1` — see SOP-NET-02)
- Community repository enabled (required for `scsi-tgt`)
- apk package index updated

To enable the community repository if not already active:

```bash
# Uncomment the /community line in the repos file
doas nano /etc/apk/repositories

# Then update the index
doas apk update
```

---

## 1. Storage Provisioning

Create a sparse 64 GB block image. This allocates near-zero physical space on disk.

```bash
doas mkdir -p /var/lib/iscsi_disks
doas truncate -s 64G /var/lib/iscsi_disks/win-san.img

# Verify apparent vs actual size
ls -lh /var/lib/iscsi_disks/win-san.img
du -sh /var/lib/iscsi_disks/win-san.img
```

---

## 2. iSCSI Target Configuration (TGT)

On Alpine, the iSCSI target stack is the `scsi-tgt` suite, available in the community repository.

```bash
# 2.1 Install the daemon, admin scripts, and OpenRC init service
doas apk add scsi-tgt scsi-tgt-scripts scsi-tgt-openrc

# 2.2 Enable and start the service
doas rc-update add tgtd default
doas rc-service tgtd start
```

Create the target configuration file:

```bash
doas mkdir -p /etc/tgt/conf.d
doas nano /etc/tgt/conf.d/win-target.conf
```

Paste the following. The IQN date and hostname can be adjusted but must stay consistent with the iPXE boot script in Section 4.

```xml
<target iqn.2026-04.local.alpine:win-target>
    backing-store /var/lib/iscsi_disks/win-san.img
    bsopts "direct_io=1"
    # Open access — no CHAP authentication
</target>
```

`tgtd` does not automatically scan `conf.d/`. Wire it into the main config:

```bash
# 2.3 Add the include directive to the main targets config
doas nano /etc/tgt/targets.conf
```

Add this line at the bottom:

```
include /etc/tgt/conf.d/*.conf
```

Load the config into the running daemon and register `tgt-admin` for persistence:

```bash
# 2.4 Execute the configuration (loads targets without a full restart)
doas tgt-admin --execute

# 2.5 Register tgt-admin to re-apply targets automatically after boot
doas rc-update add tgt-admin default
```

Verify:

```bash
doas tgtadm --mode target --op show
```

The output should list `iqn.2026-04.local.alpine:win-target` with one LUN.

---

## 3. DHCP & PXE Server (dnsmasq)

Deploy dnsmasq for DHCP and TFTP, isolated from the host's primary DNS resolver.

```bash
# 3.1 Install and prepare TFTP root
doas apk add dnsmasq
doas mkdir -p /srv/tftp

# 3.2 Back up default config
doas mv /etc/dnsmasq.conf /etc/dnsmasq.conf.bak

# 3.3 Write new config
doas nano /etc/dnsmasq.conf
```

Paste the configuration below. Replace `[INTERFACE_NAME]` with the direct-link NIC name (e.g., `eth0`). See SOP-NET-02 Section 1 for identification.

```ini
# /etc/dnsmasq.conf

# --- SERVICE ISOLATION ---
# Disable DNS — prevents conflicts with resolvers
port=0
# Bind to the isolated direct-link interface only
interface=[INTERFACE_NAME]
bind-interfaces

# --- DHCP SCOPE ---
dhcp-range=10.0.0.100,10.0.0.150,12h
dhcp-option=3,10.0.0.1          # Default gateway (this host)
dhcp-option=6,1.1.1.1           # DNS (informational only)

# --- TFTP & PXE CHAINLOAD ---
enable-tftp
tftp-root=/srv/tftp

# Serve iPXE binary to native firmware
dhcp-userclass=set:ipxe,iPXE
dhcp-boot=tag:!ipxe,ipxe.efi

# Hand off to boot script once iPXE is running
dhcp-boot=tag:ipxe,boot.ipxe
```

```bash
# 3.4 Enable and start dnsmasq
doas rc-update add dnsmasq default
doas rc-service dnsmasq start
```

---

## 4. Bootloader Staging

Stage the chainload payloads in the TFTP root.

```bash
# 4.1 Fetch the iPXE UEFI binary (x86_64 UEFI — see boot.ipxe.org/x86_64-efi/ for other arches)
doas wget -O /srv/tftp/ipxe.efi https://boot.ipxe.org/x86_64-efi/ipxe.efi

# 4.2 Write the SAN boot script
doas nano /srv/tftp/boot.ipxe
```

Replace `[HOST_IP]` with this server's static IP (default: `10.0.0.1`).

```text
#!ipxe
echo =========================================
echo  iSCSI SAN Boot — SOP-SRV-01
echo  Host: [HOST_IP]
echo =========================================

# Attach iSCSI target as primary boot drive
sanhook --drive 0x80 iscsi:[HOST_IP]::::iqn.2026-04.local.alpine:win-target || goto failed

# Boot from attached drive
sanboot --drive 0x80 || goto failed

:failed
echo SAN Boot Failed. Dropping to shell.
shell
```

---

## 5. Teardown

Revert all changes and return the host to its baseline state.

```bash
# 5.1 Stop and disable services
doas tgt-admin --delete ALL
doas rc-service tgtd stop
doas rc-update del tgtd default
doas rc-update del tgt-admin default
doas rc-service dnsmasq stop && doas rc-update del dnsmasq default

# 5.2 Remove TFTP root and dnsmasq config
doas rm -rf /srv/tftp
doas rm /etc/dnsmasq.conf
doas mv /etc/dnsmasq.conf.bak /etc/dnsmasq.conf

# 5.3 Remove iSCSI target config and storage image
doas rm /etc/tgt/conf.d/win-target.conf
doas rm -rf /var/lib/iscsi_disks

# 5.4 Uninstall packages (optional)
doas apk del scsi-tgt scsi-tgt-scripts scsi-tgt-openrc dnsmasq
```
