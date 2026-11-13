# USB link robustness

Status: investigation done 2026-11-07, `userland/homtom-netwatch` written and dry-run tested on the live device; the init.sh, ui-start and driver patches below are proposals and have not been applied or booted.

## Problem

User-reported: after a fresh boot the USB Ethernet link sometimes goes dead. The Mac shows en9 `active`, but nothing answers (ping, shell, FTP, status), while Linux keeps running (uptime counts, the LCD updates). Unplugging and replugging the cable fixed it once. On another occasion the Mac-side state flapped LINK_UP / ABSENT / LINUX by itself.

## Evidence

Measured by this investigation unless marked otherwise. Device times are uptime seconds of the boot that started at about 14:57 local time on 2026-11-07.

| # | Observation | Source |
|---|---|---|
| E1 | The reported dead link is in the Mac link log: en9 `active`, ping 0 from 14:20:18 to 14:23:52 (215 s), then USB absent for about 2 s (the replug), then ping, telnet and FTP all good. The link had worked for 51 s before it died. The device rebooted since, and dmesg lives in RAM, so no device-side record of that incident exists. | `.runtime/link/linklog-session.log` |
| E2 | Outside outages the link loses single pings constantly. The link logger (one `ping -c1 -t1` every ~2 s) flipped between ping=1 and ping=0 about 600 times an hour all night on 2026-11-07 while en9 stayed `active`. | same log, per-hour count of state changes |
| E3 | **Replies arrive one host packet late.** Ping RTTs from the Mac: at 1 s spacing a mix of ~1-10 ms and ~1000 ms (968, 1005, 1005 ms); at 0.25 s spacing a run of ~250 ms (252, 254, 255 ms). The late RTT equals the send interval, so a delayed reply goes out only when the next host packet arrives. With 40 pings at 0.25 s: 0 % loss, avg 153 ms, max 352 ms. | `ping -i 1` / `-i 0.25` against 192.168.1.10 |
| E4 | The device's IP stack is not the bottleneck: `Icmp InEchos` = `OutEchoReps` = 725. Every echo request that reached Linux was answered at once. | `/proc/net/snmp` |
| E5 | Driver code: `s3c_hsudc_epout_intr()` reads one packet per interrupt and reads the second buffer (`PSIF_TWO`) only when the first read did *not* complete the request. `s3c_hsudc_read_fifo()` completes the request on any short packet, and nearly every ECM frame is short (< 512 bytes). When both OUT buffers are full, the second packet stays in the FIFO until the next host packet raises another interrupt. This matches E3. The IN side re-arms itself (every transmitted packet raises TX_SUCCESS), so it does not show the same delay. | `.runtime/linux-trim/drivers/usb/gadget/udc/s3c-hsudc.c` lines 411-438 and 325-365 |
| E6 | The host resets the device by itself. Device dmesg at 145-152 s shows `suspend`, `resume`, `suspend`, `resume`, `reset config`, then again `suspend` and `reset config` 6 s later. The Mac log shows the gadget removed and re-added at 14:59:18 and 14:59:26, after about 45 s of the E2-style flapping. The Mac log shows no host-side reason (no user action, no sleep). This is the "flapping by itself" observation. | `dmesg` on the device; `log show` (icdd add/remove events) on the Mac |
| E7 | Every bus reset re-queues the ECM notification on ep3: the first one is written to the FIFO, and the following `notify connect` / `notify speed` sit `QUEUED waiting for TX_SUCCESS`. Not proven harmful while en9 shows `active`; it is the known cause of en9 `inactive` (docs/LESSONS.md). | dmesg `homtom: hsudc int-in ...` lines |
| E8 | Gadget configuration: g_ether has two configurations, ECM (#1) and RNDIS (#2, `CONFIG_USB_ETH_RNDIS=y` in the running trimmed kernel; the repo's `homtom_defconfig` says it is off). The Mac selects #1 (ECM). UDC `s3c-hsudc`: state `configured`, `high-speed`, `soft_connect` present. | `/sys/class/udc/s3c-hsudc/*`, dmesg, `.runtime/linux-trim/.config` |
| E9 | usb0 counters are healthy: carrier 1, `carrier_changes` 8 in the first 6 minutes (4 down, 4 up = boot, init pull-up toggle, two host resets), rx_errors 0, tx_errors 0, rx_dropped 89. The drops are IPv6 frames from the Mac (`/proc/net/if_inet6` is absent, so IPv6 is off) and are harmless. | `/sys/class/net/usb0/*`, `/proc/net/dev` |
| E10 | TCP shows the effect: `RetransSegs` 170, `EstabResets` 334 in under 10 minutes of normal agent traffic. | `/proc/net/snmp` |
| E11 | The device can probe the host. BusyBox has only the simple `ping HOST` (no `-c`/`-W`): rc 0 within 0.1 s when 192.168.1.100 answers, rc 1 after about 5 s when nothing answers. The Mac answers. | live test |
| E12 | Host side now: en9 `active`, 192.168.1.100, no input or output errors (`netstat -I en9`); the gadget shows as `RNDIS/Ethernet Gadget` on the bus. `tools/homtom-ha-bridge.py 30` and `tools/homtom-poll.py 60` run on the Mac, so the device sees a completed TCP handshake at least every 30 s while the Mac is awake. | `ifconfig`, `netstat`, `ioreg`, `ps` |

## Hypotheses

| Hypothesis | Likelihood | For | Against / open |
|---|---|---|---|
| H1. OUT-FIFO off-by-one in `s3c-hsudc` (E5) delays the newest received packet until the host sends again | **Proven as a defect** (E3, E5) | RTT = send interval; code path read | Explains latency and single losses, not a 215 s outage on its own: each new host packet pushes the held one out |
| H2. An endpoint queue in `s3c-hsudc` wedges completely (an IN request waits for a TX_SUCCESS interrupt that never comes, or OUT stops being drained), so the device stops sending or receiving until a bus reset nukes the queues | **Likely cause of the hard outage** (medium) | The fix was a replug, i.e. a bus reset, which runs `s3c_hsudc_nuke_ep()` on every endpoint; Linux stayed alive; the driver defers IN writes whenever TX_SUCCESS is already set and depends on interrupts for all progress; E3 shows that interrupt-driven progress already misbehaves | No device-side capture of an outage yet. `homtom-netwatch` logs the counter signature (`tx-stall`, `rx-stall`, `replies-lost`, `bus-silent`) and saves dmesg the next time |
| H3. The macOS ECM driver resets or gives up on the device after repeated timeouts caused by H1/H2 | Likely for the flapping (medium) | E6: resets came after ~45 s of degraded traffic, with no host-side reason in the log | Cannot see inside the Apple ECM dext; it could also be a xHCI port event |
| H4. The ECM NETWORK_CONNECTION notification is stuck (E7) | Low for this symptom | Known to cause en9 `inactive` | The reported symptom is en9 `active`, so the host did receive a connection notification |
| H5. Linux network stack, udhcpd or the listeners died | Very low | | Uptime and the LCD kept going; a replug (which does not restart userland) fixed it; E4 |
| H6. The boot-time pull-up toggle in init.sh (t = 10-13 s) races the Mac's first enumeration | Low | The reported outages are "after a fresh boot" | The 14:20 outage started about a minute after the link had come up and worked |

## Recovery actions that are available

| Action | Use | Why |
|---|---|---|
| D+ pull-up drop and raise (`/sys/kernel/homtom_usb_pullup`) | **Yes**: the only action `homtom-netwatch` takes | Identical to a replug for the host (disconnect, then bus reset). The reset nukes every endpoint queue in the driver. init.sh already does this on every boot (t = 10-13 s) |
| `soft_connect` disconnect/connect | **No** | docs/LESSONS.md: the UDC stop/start path leaves USB dead until a reboot on this board |
| UDC unbind/bind (`/sys/bus/platform/drivers/s3c-hsudc/unbind`, or writing `/sys/kernel/config/.../UDC`) | **No** | Runs the same `udc_stop` / `udc_start` path as `soft_connect`; `CONFIG_USB_CONFIGFS` is not set, so there is no configfs gadget either |
| Unload and reload g_ether or the UDC module | **Not possible** | `CONFIG_USB_ETH=y` and `CONFIG_USB_S3C_HSUDC=y` are built in |
| Reboot | Watchdog only, never automatic for the link | The user wants the link healed without a reboot; with the Mac locked a reboot also loses storage mode (docs/LESSONS.md) |

## `homtom-netwatch`

File: `userland/homtom-netwatch` (BusyBox 1.22 ash; uses only applets in `kernel/scripts/busybox-applets.txt`).

**Evidence of a working round trip**, checked every 10 s:

1. `Tcp PassiveOpens` in `/proc/net/snmp` increased. Completing a handshake needs the device's SYN-ACK to reach the host and the host's ACK to come back. The Mac's bridge and poller produce this at least every 30 s (E12). No change to statusd is needed.
2. Otherwise, after 60 s without (1), the script probes the host with three concurrent `ping 192.168.1.100`. It sends three because with H1 a single reply can stay stuck in the OUT FIFO; with three in flight at least one is pushed through.

**States** (written to `/tmp/net.state` as `STATE since-uptime detail`, and to the log on change):

| State | Condition | Action |
|---|---|---|
| OK | (1) or a probe succeeded | none |
| NOHOST | UDC `suspended`, `not attached`, or no UDC | none (cable out, Mac asleep) |
| ENUM | UDC `default` / `addressed` / `powered` | pull-up toggle after 90 s stuck |
| INACTIVE | UDC `configured` but carrier 0 (host never activated the ECM data interface) | pull-up toggle after 90 s |
| QUIET | No evidence, probe failed, and the host has not yet answered a ping this boot (it may block ICMP) | wait until 3 × 60 s, then treat as DEAD |
| DEAD | No evidence for 60 s and the probe failed | pull-up toggle, subject to the rate limit |

A carrier change (the host re-enumerated by itself, E6) restarts the 60 s window instead of counting as a failure.

**Recovery**: save `dmesg` to `INTERNAL:/homtom/logs/net-dead.dmesg`, log the counter signature, drop the pull-up for 3 s (10 s on later attempts in the same outage), raise it, wait 40 s, probe again. A trap raises the pull-up again if the script is killed mid-toggle.

**Rate limit**: at least 120 s between attempts, doubling after each failed attempt up to 30 min, at most 6 attempts per hour. A success resets the backoff.

**Signature** logged with every DEAD, counted from the last proof of a round trip, so the next outage tells H1/H2/H3 apart:

| Signature | Meaning |
|---|---|
| `tx-stall` | Packets arrive (rx grows) but the device sends nothing: IN endpoint queue wedged (H2) |
| `replies-lost` | Both counters grow, the host still does not answer: device frames do not reach the host (IN side or host driver, H2/H3) |
| `rx-stall` | UDC interrupts continue, rx does not grow: OUT side wedged (H2) |
| `bus-silent` | Neither rx nor UDC interrupts: host stopped polling (H3) or the controller is silent |

**Files**: `INTERNAL:/homtom/logs/net.log` (append, rotated to `net.log.1` at 64 KB; `/tmp/net.log` if `/mnt/internal` is not INTERNAL), `INTERNAL:/homtom/netwatch.disable` (exit at start), `INTERNAL:/homtom/netwatch.dryrun` (log only). Kernel log lines are prefixed `homtom-net:`.

**Modes**: `-n` dry run; `--once` prints counters, probes once, prints what it would do; `--test` is a dry run that writes nothing (stdout only, no lock, no state file, no dmesg capture). For tests the timings come from `NW_TICK`, `NW_SUSPECT`, `NW_SETTLE`, `NW_MINGAP`, `NW_MAXGAP`, `NW_PERHOUR`, `NW_MAXRUN`; `NW_FAKE_DEAD=1` ignores TCP evidence and fails every probe.

**Single instance**: `homtom-lock netwatch $$`; a second copy exits 0.

**Verified on the live device (dry run only, no file written, usb0/UDC untouched)**, by piping the script to `sh -s` through `tools/homtom-sh`:

- `--once`: counters printed, host answered, action none.
- `--test` with `NW_FAKE_DEAD=1` and short timings: DEAD detected, "would drop the D+ pull-up for 3s", backoff 6 s → 12 s (cap), second attempt "for 10s", then "no action: hourly cap of 2 reached" with `NW_PERHOUR=2`. UDC stayed `configured`, `carrier_changes` stayed 8, `/tmp` held no new files.
- `--test` without faking: state OK from TCP evidence; the first OK tick also probed ICMP.

**Not verified**: a real pull-up toggle by the script, and recovery from a real outage. See the hardware tests.

## init.sh supervision review

| Item | Today | Gap | Patch |
|---|---|---|---|
| telnetd, homtom-shell, ftpd, statusd | `sv` respawn every 5 s | A crash loop writes a kmsg line every 5 s forever (LCD console, dmesg ring) | `sv` backs off to 60 s after 5 quick exits and logs exits 0, 1, 10, 11, ... |
| udhcpd | started once, unsupervised | If it dies, the Mac's DHCP INIT-REBOOT on every re-enumeration (E6) gets no answer, leaving only the 169.254 alias | run `udhcpd -f` under `sv` |
| homtom-bt-start, homtom-ui-start | started once | A killed or crashed launcher never returns (the GUI stays dead until reboot) | new `svx`: restart on non-zero exit; exit 0 ("disabled" / "lock held" / bt.trying breaker) is final; `/tmp/nosv.NAME` makes a deliberate kill stick |
| homtom-netwatch | missing | | `svx`, 60 s after boot, only when a gadget interface exists |
| Watchdog feeder | gated by `/tmp/hb` | None; deliberately not tied to the link (a dead link must not reboot the unit) | unchanged |
| Single instance | locks in bt-start, ui-start; ports for listeners and statusd | none | netwatch uses `homtom-lock` |
| `/tmp/gui.log` | `>` per GUI run, never capped | Unbounded RAM use within one run; truncating it from outside does not work with `>` (the writer keeps its offset) | ui-start appends (`>>`); the init main loop truncates gui.log, btd.log and net.log above 200 KB |
| Boot logs on INTERNAL | overwritten every boot | The boot after a failure loses the failing boot's dmesg | keep `boot.dmesg.1` |
| Heartbeat line | carrier, udc, rx, tx | | adds `net=<state>` from netwatch |
| No usb0 within 30 s | listeners and statusd never start, no retry | Not patched: the gadget is built in and has always appeared within 1 s | |

The patched init.sh was extracted and passes `sh -n`; the build script passes `bash -n`. The patch adds no single quote inside the `bash -c '...'` block. It has not been run in `kernel/scripts/test-initramfs.sh` and has not been booted.

### Patch: `kernel/scripts/build-ttsystem-6.1.sh` (init.sh)

```diff
--- a/kernel/scripts/build-ttsystem-6.1.sh
+++ b/kernel/scripts/build-ttsystem-6.1.sh
@@ -52,7 +52,7 @@
 cp /out/devmem "$ROOTFS/bin/devmem"
 cp /out/devcp "$ROOTFS/bin/devcp"
 chmod 755 "$ROOTFS/bin/devcp"
-cp /out/homtom-hw /out/homtom-gpsd /out/homtom-status /out/homtom-status-legacy /out/homtom-shell /out/homtom-bt-up /out/homtom-bt-start /out/homtom-ui-start /out/homtom-lock /out/homtom-bt-discoverable /out/homtom-bt-handshake "$ROOTFS/bin/"
+cp /out/homtom-hw /out/homtom-gpsd /out/homtom-status /out/homtom-status-legacy /out/homtom-shell /out/homtom-bt-up /out/homtom-bt-start /out/homtom-ui-start /out/homtom-lock /out/homtom-bt-discoverable /out/homtom-bt-handshake /out/homtom-netwatch "$ROOTFS/bin/"
 cp /out/hcictl /out/hciattach /out/homtom-ts /out/homtom-touch /out/homtom-gui /out/homtom-bl /out/homtom-snd /out/homtom-btd /out/hcictl-info /out/homtom-statusd /out/homtom-snd2 "$ROOTFS/bin/"
 chmod 755 "$ROOTFS"/bin/homtom-* "$ROOTFS"/bin/hcictl "$ROOTFS"/bin/hciattach
 mkdir -p "$ROOTFS/lib/modules"
@@ -89,7 +89,29 @@
 rm -f /initrd.image
 k() { echo "homtom-init: $*" > /dev/kmsg 2>/dev/null; }
 ifconfig lo 127.0.0.1 up 2>/dev/null
-sv() { while :; do "$@" < /dev/null 2>&1; k "$1 exited rc=$?, restart"; sleep 5; done; }
+now() { cut -d. -f1 /proc/uptime; }
+# Daemons: respawn forever. One that keeps dying within 30 s backs off to a restart a minute and logs every tenth exit,
+# so a crash loop cannot flood the LCD console and the dmesg ring.
+sv() {
+	n=0
+	while :; do
+		t=$(now); "$@" < /dev/null 2>&1; rc=$?
+		if [ $(($(now) - t)) -lt 30 ]; then n=$((n + 1)); else n=0; fi
+		[ $((n % 10)) -le 1 ] && k "$1 exited rc=$rc, restart (quick exits: $n)"
+		if [ $n -gt 5 ]; then sleep 60; else sleep 5; fi
+	done
+}
+# Launchers: exit 0 means "not wanted here" (disabled, or another instance holds the lock) and is final; anything else,
+# a kill included, restarts. touch /tmp/nosv.NAME before killing NAME by PID to stop it for good.
+svx() {
+	while :; do
+		"$@" < /dev/null > /dev/null 2>&1 && break
+		rc=$?
+		[ -f /tmp/nosv.$1 ] && break
+		k "$1 exited rc=$rc, restart"
+		sleep 10
+	done
+}
 # Safety net: TTBL treats a reset with INFORM0=5 as "Reboot" (then storage mode); a stopped feeder lets the watchdog reset a hung unit.
 devmem 0x4C000070 5 > /dev/null 2>&1
 [ -c /dev/watchdog ] || k "no /dev/watchdog"
@@ -118,7 +140,8 @@
 	echo "interface $IF" >> /etc/udhcpd.conf
 	echo "opt subnet 255.255.255.0" >> /etc/udhcpd.conf
 	echo "lease_file /var/run/udhcpd.leases" >> /etc/udhcpd.conf
-	udhcpd /etc/udhcpd.conf </dev/null >/dev/null 2>&1 &
+	# the Mac re-runs DHCP on every re-enumeration; without a server it falls back to 169.254 only
+	( sv udhcpd -f /etc/udhcpd.conf > /dev/null 2>&1 ) &
 	# Listeners (each in the background; an exit is reported to the console): telnet 23, raw shell 2323, FTP 21.
 	( sv telnetd -F -l /bin/sh -p 23 ) &
 	( sv tcpsvd 0.0.0.0 2323 /bin/homtom-shell ) &
@@ -174,6 +197,8 @@
 dmesg > /tmp/boot.dmesg 2>/dev/null
 if [ -n "$LOGDIR" ]; then
 	mkdir -p "$LOGDIR/homtom/logs" 2>/dev/null
+	# keep one previous boot: a link failure is only diagnosed on the boot after it
+	mv -f "$LOGDIR/homtom/logs/boot.dmesg" "$LOGDIR/homtom/logs/boot.dmesg.1" 2>/dev/null
 	cp /tmp/boot.log "$LOGDIR/homtom-boot.log" 2>/dev/null
 	cp /tmp/boot.dmesg "$LOGDIR/homtom-boot.dmesg" 2>/dev/null
 	cp /tmp/boot.log "$LOGDIR/homtom/logs/boot.log" 2>/dev/null
@@ -181,14 +206,20 @@
 	sync
 fi
 
-( sleep 12; homtom-bt-start ) > /dev/null 2>&1 < /dev/null &
-( sleep 15; homtom-ui-start ) > /dev/null 2>&1 < /dev/null &
+( sleep 12; svx homtom-bt-start ) > /dev/null 2>&1 < /dev/null &
+( sleep 15; svx homtom-ui-start ) > /dev/null 2>&1 < /dev/null &
+# USB link self-healing (docs/ROBUSTNESS.md); starts well after the boot-time pull-up toggle and first enumeration
+[ -n "$IF" ] && ( sleep 60; svx homtom-netwatch ) > /dev/null 2>&1 < /dev/null &
 # Never exit: PID 1 dying panics the kernel (there is no console, so a shell would see EOF).
 # Every 30 s print one heartbeat line to the LCD console via /dev/kmsg (the old per-5-s dmesg greps echoed their own output).
 while :; do
 	read up rest < /proc/uptime
 	echo "$up" > /tmp/hb
-	k "up=$up carrier=$(cat /sys/class/net/$IF/carrier 2>&1) udc=$(cat /sys/class/udc/*/state 2>&1) $IF rx=$(cat /sys/class/net/$IF/statistics/rx_packets 2>&1) tx=$(cat /sys/class/net/$IF/statistics/tx_packets 2>&1)"
+	k "up=$up carrier=$(cat /sys/class/net/$IF/carrier 2>&1) udc=$(cat /sys/class/udc/*/state 2>&1) $IF rx=$(cat /sys/class/net/$IF/statistics/rx_packets 2>&1) tx=$(cat /sys/class/net/$IF/statistics/tx_packets 2>&1) net=$(cut -d" " -f1 /tmp/net.state 2>/dev/null)"
+	# /tmp is RAM: cap logs that services append to
+	for f in /tmp/gui.log /tmp/btd.log /tmp/net.log; do
+		[ "$(wc -c < $f 2>/dev/null || echo 0)" -gt 200000 ] && : > $f
+	done
 	sleep 30
 done
 EOF
```

Note for `svx homtom-ui-start`: docs/LESSONS.md says to kill duplicate launchers by PID. With this patch, `touch /tmp/nosv.homtom-ui-start` first, or the launcher returns after 10 s.

### Patch: `userland/homtom-ui-start`

```diff
--- a/userland/homtom-ui-start
+++ b/userland/homtom-ui-start
@@ -10,6 +10,6 @@
 		up) echo up ;;
 		*) echo t $x $y ;;
 		esac
-	done | homtom-gui > /tmp/gui.log 2>&1
+	done | homtom-gui >> /tmp/gui.log 2>&1
 	sleep 2
 done
```

## Proposed driver fix for H1 (not built, not booted)

Drain the second OUT buffer in the same interrupt. The giveback drops the lock and may select another endpoint, so the patch selects this endpoint again and checks a fresh status before reading. During giveback the endpoint is marked `stopped`, so a re-queue from `rx_complete` only appends and never reads the FIFO itself; no double read is possible. It would go into `kernel/scripts/apply-homtom.sh` next to the other `s3c-hsudc` edits.

```diff
--- a/drivers/usb/gadget/udc/s3c-hsudc.c
+++ b/drivers/usb/gadget/udc/s3c-hsudc.c
@@ -431,9 +431,22 @@
 
 		hsreq = list_entry(hsep->queue.next,
 				struct s3c_hsudc_req, queue);
-		if (((s3c_hsudc_read_fifo(hsep, hsreq)) == 0) &&
-				(csr & S3C_ESR_PSIF_TWO))
-			s3c_hsudc_read_fifo(hsep, hsreq);
+		s3c_hsudc_read_fifo(hsep, hsreq);
+		/*
+		 * Both FIFO buffers can be full behind one interrupt. A short packet (nearly every ECM frame) completes the
+		 * request above, so the second packet used to wait for the next host packet: replies arrived one host
+		 * packet late. Drain it now. The giveback may have reselected another endpoint, so select ours again and
+		 * trust only a fresh status read.
+		 */
+		if ((csr & S3C_ESR_PSIF_TWO) && !list_empty(&hsep->queue)) {
+			set_index(hsudc, ep_idx);
+			csr = readl(hsudc->regs + S3C_ESR);
+			if (csr & S3C_ESR_RX_SUCCESS) {
+				hsreq = list_entry(hsep->queue.next,
+						struct s3c_hsudc_req, queue);
+				s3c_hsudc_read_fifo(hsep, hsreq);
+			}
+		}
 	}
 }
```

Acceptance test for that image: `ping -i 0.25 -c 200 192.168.1.10` and `ping -i 1 -c 60` from the Mac show no RTT near the send interval, and `tools/homtom-linklog` shows no ping 0/1 alternation over an hour. Open question for H2: the IN path defers a write whenever TX_SUCCESS is already set at queue time and relies on the next interrupt; if netwatch logs `tx-stall`, look there first.

## Recovery on the device without the host

**Link state for a local screen.** `homtom-netwatch` keeps `/tmp/net.state` (`OK`, `NOHOST`, `ENUM`, `INACTIVE`, `QUIET`, `DEAD`, with the uptime it entered that state and a short detail). Proposal for the GUI (not implemented): when the state is not `OK` for more than 2 minutes, show a "safe mode" banner with the state, how long it has lasted, how many recoveries were tried, and two touch buttons: "Reconnect USB" (runs the same pull-up toggle: `echo 0 > /sys/kernel/homtom_usb_pullup; sleep 3; echo 1 > ...`) and "Restart to storage" (runs `/bin/reboot`, the INFORM0=5 wrapper). The GUI owns the LCD, so this belongs in `homtom-gui`, not in a shell script that would fight it.

**Long power-key press.** Already in the kernel (`homtom_pwrkey_poll` in `mach-homtom-go630.c`): holding the key for 2 s calls `kernel_power_off()`. On battery that drives PWR_RST (GPJ10) and powers off. On USB power it falls through to `homtom_restart()` (INFORM0=5, SWRST, then watchdog), which lands in the bootloader's Reboot path and, with an unlocked Mac, in storage mode. Two caveats from docs/LESSONS.md: the in-kernel restart path did not bring the unit back when reached via `reboot -f` (cause not found), and 2 s is short enough to trigger by accident. Proposal: 2 s does nothing, 5 s calls the restart (INFORM0=5 first, then let the watchdog fire, the path verified from userspace), and 10 s powers off. This is a kernel change and needs its own image and hardware test.

**Watchdog.** Unchanged: the feeder stops when `/tmp/hb` is stale for about 4 minutes, so a wedged init resets the unit into the Reboot path. The link is deliberately not part of this: a dead link with a healthy unit must not reboot (no storage mode when the Mac is locked, and the reboot loses the evidence in RAM). If an automatic last resort is ever wanted, it should be an explicit opt-in file (for example `INTERNAL:/homtom/netwatch.reboot`) that allows one reboot after several failed pull-up recoveries over at least 30 minutes. That is not implemented.

## Hardware tests

1. **Pull-up recovery by hand while the link works** (only with nobody else using the link): run `homtom-sh 'echo 0 > /sys/kernel/homtom_usb_pullup; sleep 3; echo 1 > /sys/kernel/homtom_usb_pullup' 2` in the background and watch `tools/homtom-state`. Expected: ABSENT or LINK_UP for a few seconds, then LINUX within about 15 s. This is the action the script takes. It is the same sequence init.sh runs at every boot, but it has not been run on a live link with the Mac's network already configured.
2. **Netwatch dry run on the device** (no image change): upload with `tools/homtom-put userland/homtom-netwatch /tmp/homtom-netwatch`, then on the device `touch /mnt/internal/homtom/netwatch.dryrun; sh /tmp/homtom-netwatch < /dev/null > /dev/null 2>&1 &`. /tmp is RAM, so a reboot removes it. Leave it for a few hours of normal use. `INTERNAL:/homtom/logs/net.log` should show only `state OK` and no DEAD. Any DEAD line with a signature is the first device-side record of an outage.
3. **Real recovery**: remove `netwatch.dryrun` and restart the script. When the link dies (or after reproducing it by repeated fresh boots), expect `recover #1`, then `recovered` within about a minute, with no replug. Keep `net.log` and `net-dead.dmesg` from INTERNAL.
4. **Mac asleep / cable out**: expect `NOHOST` and no recovery attempts in `net.log`.
5. **Image with the init.sh patch**: after boot, check `ps` for one `homtom-netwatch`, `udhcpd -f`, and the `net=` field in the heartbeat. Kill `homtom-ui-start` by PID: it should return within 10 s. With `/tmp/nosv.homtom-ui-start` it should not. Run `kernel/scripts/test-initramfs.sh 80` first.
6. **Driver fix image** (separately, its own image): the acceptance test above. Then run the link logger overnight and compare the hourly flip count with E2.
