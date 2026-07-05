# Ubuntu Server Boot Configuration

> [!abstract] Purpose
> This document defines all system-level configuration required to make the Projection cinema server boot directly into operational state from power-off, with no human interaction required.

---

## 1. OS Selection

**Ubuntu Server 22.04 LTS** (headless — no desktop environment installed).

```bash
# Verify no desktop packages are installed
dpkg -l ubuntu-desktop gnome-shell xorg 2>/dev/null | grep ^ii
# All should return nothing
```

---

## 2. Static Network Configuration (netplan)

Edit `/etc/netplan/00-installer-config.yaml`:

```yaml
network:
  version: 2
  ethernets:
    # Control / Management NIC (Christie, CP950A, MQTT, signage)
    enp3s0:
      dhcp4: false
      addresses:
        - 192.168.1.10/24
      routes:
        - to: default
          via: 192.168.1.1
      nameservers:
        addresses: [8.8.8.8, 1.1.1.1]

    # Dedicated AES67 / Dante NIC (isolated network, no gateway)
    enp4s0:
      dhcp4: false
      addresses:
        - 169.254.10.1/16   # Dante link-local compatible range
```

```bash
sudo netplan apply
```

---

## 3. Disable Audio Daemons

PulseAudio and PipeWire will claim the RME card before LibVLC can open it with `hw:` direct access. Disable both system-wide:

```bash
# Disable PulseAudio
sudo systemctl --global disable pulseaudio.service pulseaudio.socket
sudo systemctl mask pulseaudio.service pulseaudio.socket

# Disable PipeWire (Ubuntu 22.04+ ships it)
sudo systemctl --global disable pipewire.service pipewire-pulse.service wireplumber.service
sudo systemctl mask pipewire.service pipewire-pulse.service wireplumber.service

# Prevent user-level re-enable
sudo bash -c 'echo "autospawn = no" >> /etc/pulse/client.conf'
```

---

## 4. Blackmagic DeckLink Driver

```bash
# Install Desktop Video 16.x (download requires developer account)
sudo dpkg -i desktopvideo_16.0.1_amd64.deb
sudo apt-get install -f

# Persist kernel module across reboots
echo "blackmagic" | sudo tee /etc/modules-load.d/blackmagic.conf
echo "blackmagic-io" | sudo tee -a /etc/modules-load.d/blackmagic.conf

# Verify after reboot
blackmagic-io-firmware-updater --status
```

> [!warning] Kernel Update Behavior
> The Blackmagic driver is an **out-of-tree** kernel module. After any `apt upgrade` that updates the kernel, run `sudo dpkg-reconfigure desktopvideo` or reinstall the package to rebuild the module for the new kernel.

---

## 5. RME HDSPe AES — No Additional Setup Required

The RME Linux ALSA driver is included in the mainline kernel (`snd-hdspe` module). It loads automatically when the card is detected at boot.

```bash
# Verify driver is loaded
lsmod | grep snd_hdspe

# Verify ALSA sees the card (run after boot)
aplay -l | grep HDSPe
```

The card restores its last-used sync configuration from non-volatile on-card storage at power-on.

---

## 6. MQTT Broker (Mosquitto)

The cinema engine connects to a broker at `192.168.1.50`. If that broker runs on this server:

```bash
sudo apt install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

If the broker runs on a separate device (recommended for redundancy), ensure it is on the same subnet and reachable before the cinema service starts.

---

## 7. Web Server for Marquee (nginx)

The marquee system writes files to `/var/www/marquee/`. A lightweight HTTP server serves them to lobby display hardware:

```bash
sudo apt install nginx

# Create the marquee directory
sudo mkdir -p /var/www/marquee
sudo chown cinema:www-data /var/www/marquee
sudo chmod 775 /var/www/marquee

# Add nginx location block to /etc/nginx/sites-available/default:
```

```nginx
server {
    listen 80 default_server;
    root /var/www;

    location /marquee/ {
        autoindex on;
        add_header Cache-Control "no-cache, no-store, must-revalidate";
    }
}
```

```bash
sudo systemctl enable nginx
sudo nginx -t && sudo systemctl restart nginx
```

---

## 8. Create Cinema User and Directories

```bash
# Dedicated service account (no login shell)
sudo useradd -r -s /usr/sbin/nologin -d /opt/cinema cinema

# Application directories
sudo mkdir -p /opt/cinema/bin
sudo mkdir -p /opt/cinema/playlist
sudo mkdir -p /var/log/cinema
sudo mkdir -p /var/www/marquee

sudo chown -R cinema:cinema /opt/cinema /var/log/cinema
sudo chown -R cinema:www-data /var/www/marquee
sudo chmod 775 /var/www/marquee
```

---

## 9. systemd Service Unit

Save as `/etc/systemd/system/sdi_cinema.service`:

```ini
[Unit]
Description=SDI Cinema Media Engine
Documentation=https://github.com/your-org/projection
After=network-online.target mosquitto.service sound.target
Wants=network-online.target
# Require the DeckLink and RME modules to be loaded
After=systemd-modules-load.service

[Service]
Type=simple
User=cinema
Group=cinema

# Playlist directory is watched; files passed as arguments
# Format: sdi_cinema_engine /opt/cinema/playlist/01_feature.mp4 [...]
ExecStartPre=/bin/sleep 10
ExecStartPre=/bin/bash -c 'test -d /var/www/marquee || mkdir -p /var/www/marquee'
ExecStart=/opt/cinema/bin/sdi_cinema_engine /opt/cinema/playlist/*.mp4

# Restart policy — restart 5 seconds after crash
Restart=on-failure
RestartSec=5s

# Resource limits
LimitNOFILE=65536
LimitMEMLOCK=infinity
LimitRTPRIO=99

# Real-time scheduling for audio-critical thread
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=50

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=sdi_cinema

# Environment
Environment=HOME=/opt/cinema
Environment=VLC_PLUGIN_PATH=/usr/lib/vlc/plugins

[Install]
WantedBy=multi-user.target
```

```bash
# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable sdi_cinema
sudo systemctl start sdi_cinema

# Monitor
journalctl -u sdi_cinema -f
```

> [!note] ExecStartPre Sleep
> The 10-second `sleep` in `ExecStartPre` gives the Brainstorm DXD-16, DeckLink, and RME cards time to stabilize their clock locks after power-on before the engine attempts to open hardware devices. Tune this value based on observed DXD-16 lock acquisition time.

---

## 10. GRUB Boot Optimization

Reduce boot time by removing unnecessary wait states:

```bash
# Edit /etc/default/grub
sudo nano /etc/default/grub
```

```ini
# Reduce GRUB timeout
GRUB_TIMEOUT=2

# Quiet boot (suppress kernel messages on display)
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"

# Real-time kernel tuning (optional, for lowest latency)
GRUB_CMDLINE_LINUX="threadirqs mitigations=off"
```

```bash
sudo update-grub
```

---

## 11. Kernel Real-Time Tuning (Optional)

For the lowest possible audio scheduling jitter:

```bash
# Install low-latency kernel
sudo apt install linux-lowlatency

# Set CPU governor to performance
sudo apt install cpufrequtils
echo 'GOVERNOR="performance"' | sudo tee /etc/default/cpufrequtils
sudo systemctl enable cpufrequtils

# Disable CPU idle states for audio thread (reduces wake-up latency)
# Add to /etc/rc.local or a dedicated systemd service:
for cpu in /sys/devices/system/cpu/cpu*/cpuidle/state*/disable; do
    echo 1 > $cpu 2>/dev/null || true
done
```

---

## 12. Boot Health Check Script

Add a pre-start health check that verifies hardware lock before the engine launches:

Save as `/opt/cinema/bin/check_hardware.sh`:

```bash
#!/bin/bash
# Pre-flight check: verify DeckLink and RME are ready

# Check DeckLink driver loaded
if ! lsmod | grep -q blackmagic; then
    echo "[PREFLIGHT FAIL] DeckLink kernel module not loaded" >&2
    exit 1
fi

# Check RME ALSA device exists
if ! aplay -l 2>/dev/null | grep -q HDSPe; then
    echo "[PREFLIGHT FAIL] RME HDSPe AES not detected by ALSA" >&2
    exit 1
fi

# Check MQTT broker reachable
if ! nc -z -w 2 192.168.1.50 1883; then
    echo "[PREFLIGHT WARN] MQTT broker not reachable at 192.168.1.50:1883" >&2
    # Non-fatal — engine can operate without MQTT for video/audio
fi

# Check Christie TCP port reachable
if ! nc -z -w 2 192.168.1.75 3002; then
    echo "[PREFLIGHT WARN] Christie projector not reachable at 192.168.1.75:3002" >&2
fi

echo "[PREFLIGHT OK] Hardware checks passed"
exit 0
```

```bash
sudo chmod +x /opt/cinema/bin/check_hardware.sh
```

Update the service unit to call it:

```ini
ExecStartPre=/opt/cinema/bin/check_hardware.sh
ExecStartPre=/bin/sleep 10
```

---

## Boot Sequence Summary

```text
Power On
    │
    ├─ DXD-16 powers on (UPS priority circuit)
    │   └─ Generates Tri-Level Sync + Word Clock
    │
    ├─ Ubuntu Server POST + GRUB (2s)
    │
    ├─ Kernel loads
    │   ├─ blackmagic module → DeckLink locks to DXD-16 Genlock
    │   └─ snd-hdspe module  → RME locks to DXD-16 Word Clock
    │
    ├─ systemd starts services
    │   ├─ network-online.target (static IPs apply)
    │   ├─ mosquitto (MQTT broker)
    │   └─ nginx (marquee web server)
    │
    ├─ sdi_cinema.service starts
    │   ├─ ExecStartPre: check_hardware.sh (verify locks)
    │   ├─ ExecStartPre: sleep 10s (clock stabilization)
    │   └─ ExecStart: sdi_cinema_engine *.mp4
    │
    └─ Engine running — first title playing, marquee populated
```

---

*See also: [[Projection Spec]], [[Blackmagic DeckLink 8K Pro G2]], [[RME HDSPe AES]], [[Brainstorm DXD-16]]*
