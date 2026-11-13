# Key-only SSH on the USB link

Status (2026-11-07): built and tested on the Mac only, under qemu's ARMv4T CPU model. Not in the boot image, never run on the device. The telnet (23), raw shell (2323) and FTP (21) listeners are unchanged.

| Piece | Where |
|---|---|
| Toolchain and server build, verification | `scripts/build-armv4t-toolchain.sh` (configs in `build-env/armv4t/`) |
| Build outputs (not tracked) | `artifacts/toolchain-armv4t/`: `armv4t-toolchain.tar.xz`, `dropbear/dropbearmulti`, `verify.log`, `launcher.log`, `hostssh.log` |
| Device launcher | `userland/homtom-sshd` |
| User files on INTERNAL | `homtom/authorized_keys` (you write it), `homtom/ssh/` (the device writes it) |

## 1. Toolchain

The device's ARM920T is ARMv4T. Debian armel and its cross compiler target ARMv5TE, and their `libgcc` and libc contain `clz`, `blx` and `ldrd`, which trap on this CPU. The existing freestanding tools avoid libc entirely; a server needs one.

The build uses Buildroot `2026.02.3` (LTS) to build only a toolchain: `BR2_arm920t`, EABI, ARM instructions, uClibc-ng, static libraries, Linux 6.1 headers (`build-env/armv4t/buildroot.defconfig`). GCC is configured with `--with-cpu=arm920t`, so libgcc, the C start-up files and uClibc-ng are all ARMv4T. uClibc-ng was chosen over musl because it is the default and best-tested Buildroot library on pre-v6 ARM, and on ARMv4T it uses the kernel's user helpers (`CONFIG_KUSER_HELPERS=y` in our kernel, forced by `NEED_KUSER_HELPERS`) instead of carrying ARMv6/v7 paths selected at run time.

Everything runs in a pinned `debian:bookworm` image on the Mac's native platform. Source tarballs are checked against SHA-256 values in the script and cached in `artifacts/toolchain-armv4t/dl/`. The toolchain is packed as `armv4t-toolchain.tar.xz` (it unpacks to `/opt/armv4t`, the path it was built for) and is not committed.

```sh
scripts/build-armv4t-toolchain.sh             # all stages; about 20 min the first time, toolchain reused after that
scripts/build-armv4t-toolchain.sh dropbear    # rebuild only the server
scripts/build-armv4t-toolchain.sh verify      # instruction audit and qemu tests
```

Use it for other userland C as well: `PATH=/opt/armv4t/bin:$PATH arm-buildroot-linux-uclibcgnueabi-gcc -static -Os ...` inside a container that has the tarball unpacked to `/opt`.

### Verification (`verify` stage)

For the unstripped server and a libc probe:

- `readelf -A`: `Tag_CPU_arch: v4T`; no `PT_INTERP` (static).
- Disassembly (with mapping symbols, so literal pools are not decoded): no ARMv5 or later mnemonic (`clz blx ldrd strd pld bkpt smulxy smlaxy qadd ldrex strex rev sxt uxt movw movt dmb ubfx sdiv`, any VFP `v*`, and the rest of the list in the script), no access to the ARMv6K TLS register (`cr13`), no Thumb-2. `smull`/`smlal`/`umull`/`umlal` are ARMv4 long multiplies and allowed. One exception is listed, not counted: libgcc's unwinder links six `__gnu_Unwind_Save/Restore_VFP*` helpers (`vldmia`/`vstmia`). They run only while unwinding a frame that saved VFP registers, which soft-float code never creates, and nothing in these programs throws or unwinds.
- Run under `qemu-arm -cpu ti925t`. qemu has no ARM920T model; the TI925T is its only ARMv4T core and implements no ARMv5 instruction. A control build of the same probe with `-march=armv5te` and one `clz` must die with SIGILL there, which shows the model really rejects ARMv5.

Result (2026-11-07, `artifacts/toolchain-armv4t/verify.log`, `launcher.log`, `hostssh.log`):

| Check | Result |
|---|---|
| Toolchain | GCC 14.3.0, uClibc-ng, binutils from Buildroot 2026.02.3; first build 16.5 min on 17 cores; `armv4t-toolchain.tar.xz` 41.7 MB |
| `dropbearmulti` (stripped, shipped) | 222,912 bytes, sha256 `03cccf0bf71ba0b2d23f5cdab97bad8d8e4e2eb74c777cbff0cef672578156c1` |
| `dropbearmulti.debug` (audited) | 339,280 bytes, sha256 `12f50a3ade501271c0b61f82d3ac6ddffbb2f0fb37caecb034e3fd823ac831e8` |
| Reproducible | two separate builds gave identical hashes for all four outputs |
| ELF | `EXEC`, static, `Version5 EABI, soft-float`, `Tag_CPU_arch: v4T`, `Tag_THUMB_ISA_use: Thumb-1` |
| Instruction audit | 48,736 instructions in the server, 9,968 in the probe: none ARMv5 or later, no Thumb-2, no `cr13` |
| qemu 7.2 `-cpu ti925t` | probe: stdio, `getpwnam`, `getrandom`, 64-bit division OK; ARMv5TE control: SIGILL; `dropbear -V`: `Dropbear v2026.94`; `dropbearkey -t ed25519` OK, `-t rsa` refused |
| Server under qemu, OpenSSH 9.2 client | key login OK; only `publickey` offered; unknown key, password and keyboard-interactive refused; `-W` forwarding refused; kex `curve25519-sha256`, `chacha20-poly1305` |
| Server under qemu, this Mac's OpenSSH 10.2 | key login OK; kex `mlkem768x25519-sha256`, host key `ssh-ed25519`, no post-quantum warning |
| `homtom-sshd` (BusyBox ash, server under qemu) | refuses to start without `authorized_keys` and with a private key in it; CRLF file with a comment, an `ssh-rsa` line and an options line gives 1 key; creates `passwd`/`shells`; host key made and saved; login OK with the pinned key; listens on `192.168.1.10%lo:22` only; second instance exits; killed dropbear restarted; host key and fingerprint unchanged after a simulated reboot; no restart once `authorized_keys` is gone |

The launcher test ran with Debian's BusyBox, not the device's 1.22.1; the launcher was checked by hand to use only applets in `busybox-applets.txt`.

## 2. The server

`dropbear` `2026.94`, built as one static `dropbearmulti` holding only `dropbear` and `dropbearkey` (`build-env/armv4t/dropbear-localoptions.h`):

| Area | Built in | Left out |
|---|---|---|
| Login | public key | password, PAM, `authorized_keys` options |
| Keys | Ed25519 host and user keys | RSA, ECDSA, DSS, security-key (`sk-`) keys |
| Key exchange | `mlkem768x25519-sha256`, `curve25519-sha256` | sntrup761, NIST ECDH, all finite-field DH |
| Cipher, MAC | `chacha20-poly1305` (MAC is part of it), `hmac-sha2-256` | AES, 3DES |
| Channels | session (shell, command) | X11, agent, TCP and stream forwarding, SFTP, inetd mode, MOTD |
| Per connection | runs in the forked child; no re-exec (the binary is static and not position independent, so re-exec would only re-randomise stack and heap, at the cost of a full exec per login) | |

The post-quantum `mlkem768x25519` method stays because OpenSSH 10 (the Mac has 10.2) prints a warning on every connection that does not use one.

## 3. Launcher `userland/homtom-sshd`

`homtom-sshd [IF]` (default `usb0`), BusyBox ash, only applets from `kernel/scripts/busybox-applets.txt` plus `homtom-lock`, `dropbear` and `dropbearkey`.

1. Exits quietly unless `/mnt/internal` is INTERNAL (`ttsystem.lab2613` marker, as `homtom-bt-start` does) and `dropbear` exists. Takes the `sshd` lock (`homtom-lock`), so a second start exits.
2. Adds `root:x:0:0:root:/root:/bin/sh` to `/etc/passwd` and `/bin/sh` to `/etc/shells` when missing. dropbear looks the user up with `getpwnam` and rejects a shell not listed in `/etc/shells`. `x` with no `/etc/shadow` matches no password.
3. Reads `INTERNAL:/homtom/authorized_keys`. Missing file, a file containing `PRIVATE KEY`, or no usable line: logs why and exits; the service stays off until the next boot. Usable lines are `ssh-ed25519 AAAAC3NzaC1lZDI1NTE5…` with an optional comment; CR line ends are tolerated; other key types, comments and lines with options are dropped. The result goes to `/root/.ssh/authorized_keys` (mode 600) in RAM: dropbear refuses a key file in a group- or world-writable path, and FAT has no modes.
4. Host key: see below.
5. Runs `dropbear -F -E -s -g -j -k -m -T 3 -K 30 -l IF -p 192.168.1.10:22 -r /etc/dropbear/dropbear_ed25519_host_key -P /var/run/dropbear.pid`. `-s` and `-g` refuse password logins (the code is not built in either; the flags guard a future rebuild), `-j -k` refuse forwarding, `-l` binds the socket to the gadget interface so the address cannot be reached through another interface. When dropbear exits, it is restarted after 5 s; `authorized_keys` is read again before each restart.

Log: `/tmp/sshd.log` (capped at 100 kB) and `homtom-sshd:` lines in `dmesg`.

### Host key: persisted on INTERNAL

The key is created with `dropbearkey -t ed25519` on the first start, used from `/etc/dropbear/` (RAM), and copied to `INTERNAL:/homtom/ssh/ed25519_host_key`. The device also writes `homtom/ssh/ed25519_host_key.pub` and `homtom/ssh/fingerprint`. A copy that `dropbearkey -y` cannot read is renamed to `.bad` and a new key is made (clients then see a changed key, which is the correct signal).

Alternatives considered:

| Option | Problem |
|---|---|
| New key every boot, RAM only | The fingerprint changes on every boot, so the Mac must turn off host key checking, and then nothing detects a substitute device. Also costs a key generation (entropy, CPU) per boot. |
| Key generated at image build time, inside the initramfs | `ttsystem` lies on INTERNAL anyway, so it is exposed in storage mode the same way; every unit flashed with the same image (and any published image) would share one host key. |
| **Persisted on INTERNAL** (chosen) | INTERNAL is a FAT volume: in storage mode anyone with the device and a USB cable can read the private host key. |

The exposure in the chosen option buys an attacker nothing new: whoever can read INTERNAL in storage mode can also replace `ttsystem` and `homtom/authorized_keys`, which is full control of the device. The host key's job here is to make the Mac notice a different device or a reinstalled one, and a stable per-unit key does that. To rotate: in storage mode delete `homtom/ssh/`, boot, and pin the new key.

## 4. Mac side

One-time setup:

```sh
ssh-keygen -t ed25519 -f ~/.ssh/homtom_ed25519 -C homtom     # passphrase recommended; the private key never leaves the Mac
```

`~/.ssh/config`:

```
Host homtom
	HostName 192.168.1.10
	User root
	IdentityFile ~/.ssh/homtom_ed25519
	IdentitiesOnly yes
	HostKeyAlias homtom
	UserKnownHostsFile ~/.ssh/known_hosts.homtom
	StrictHostKeyChecking yes
	ControlMaster auto
	ControlPath ~/.ssh/cm-%C
	ControlPersist 10m
```

`ControlMaster` keeps one connection open, so later `ssh homtom ...` calls skip the key exchange (slow on a 200 MHz ARM9; see open issues).

In storage mode (`/Volumes/INTERNAL`):

```sh
mkdir -p /Volumes/INTERNAL/homtom
cp ~/.ssh/homtom_ed25519.pub /Volumes/INTERNAL/homtom/authorized_keys     # the .pub file only
```

Boot Linux once with SSH in the image, return to storage mode, and pin the host key from the volume (no trust-on-first-use over the link):

```sh
echo "homtom $(cat /Volumes/INTERNAL/homtom/ssh/ed25519_host_key.pub)" >> ~/.ssh/known_hosts.homtom
cat /Volumes/INTERNAL/homtom/ssh/fingerprint
```

Then `ssh homtom` (equivalent to `ssh root@192.168.1.10` with the options above). Copying files without SFTP or scp on the device:

```sh
ssh homtom 'cat > /tmp/file' < file          # upload
ssh homtom 'cat /tmp/file' > file            # download
ssh homtom 'wc -c < /tmp/file'               # size check (no sha256sum or md5sum applet on the device)
```

With OpenSSH 9.0 or later, `scp` defaults to SFTP and fails; `scp -O` uses the legacy protocol, which needs an `scp` binary on the device that this build does not include.

## 5. Threat model

| Item | Position |
|---|---|
| Reachability | `192.168.1.10:22` on the USB gadget interface only (`-l`), which exists only while the device is cabled to a host. Not on the `169.254.10.10` fallback address, not on Bluetooth or any future network. |
| Authentication | Ed25519 public keys from `INTERNAL:/homtom/authorized_keys`, written by the user in storage mode. No passwords exist. Three attempts per connection, at most four unauthenticated connections. |
| Who can add a key | Anyone who can write INTERNAL: storage mode (physical access), or a root shell on the device. Physical access already means full control (the bootloader accepts any `ttsystem`), so SSH does not try to defend against it. |
| Host key | Readable in storage mode (section 3). Lets someone with physical access impersonate this unit to the Mac; it does not give access to the Mac. |
| Client key | Stays on the Mac. The launcher refuses to start when the device file contains a private key, so a mistaken copy is noticed. |
| What SSH changes today | Nothing is closed yet. Telnet 23, raw shell 2323 and FTP 21 still give unauthenticated root to any process on the Mac that can reach the link. SSH only becomes a security improvement once those are off (section 7). |
| Not covered | The status HTTP server on 8080 (read-only, unauthenticated). The bootloader's storage mode itself. |

## 6. Proposed integration (not applied)

`kernel/scripts/build-ttsystem-6.1.sh` is not changed by this work. Proposed edits:

Host side, next to the other `tools/devtools` copies (after copying the built binary to `tools/devtools/dropbearmulti`, like the other device binaries):

```sh
cp "$ROOT/tools/devtools/dropbearmulti" "$OUT/dropbearmulti"
```

In the rootfs container, after the `homtom-*` copies:

```sh
cp /out/homtom-sshd "$ROOTFS/bin/"
cp /out/dropbearmulti "$ROOTFS/bin/dropbearmulti"
chmod 755 "$ROOTFS/bin/homtom-sshd" "$ROOTFS/bin/dropbearmulti"
ln -sf dropbearmulti "$ROOTFS/bin/dropbear"
ln -sf dropbearmulti "$ROOTFS/bin/dropbearkey"
printf 'root:x:0:0:root:/root:/bin/sh\n' > "$ROOTFS/etc/passwd"
echo /bin/sh > "$ROOTFS/etc/shells"
mkdir -p "$ROOTFS/root" && chmod 700 "$ROOTFS/root"
```

(The launcher creates `passwd`, `shells` and `/root` itself when they are missing, so the last three lines are optional.)

`init.sh`, after the INTERNAL mount block, next to the other delayed starts (it needs INTERNAL mounted and the gadget address up):

```sh
[ -n "$IF" ] && ( sleep 10; homtom-sshd "$IF" ) > /dev/null 2>&1 < /dev/null &
```

and, in the listener message: `NETMSG="net: $IF 192.168.1.10, ssh 22 / telnet 23 / raw 2323 / ftp 21"`.

Size cost in the initramfs: one 222,912-byte static binary plus the 4 kB launcher; nothing else (no shared libraries).

## 7. Turning off telnet, raw shell and FTP later

Everything on the Mac that talks to the device today uses one of those three ports:

| Uses | Tools |
|---|---|
| Raw shell 2323 via `tools/homtom-sh` | `homtom-sh` itself; callers `homtom-draw`, `homtom-mark-good`, `homtom-to-storage`, `homtom-touch-run`, `homtom-tap`, `homtom-screen`, `homtom-ha-bridge.py`; docs and snippets that tell you to run it (`scripts/make-release.sh` notes, `workspace/perf/boot-marks.sh`, `tools/host/README.md`) |
| FTP 21 upload via `tools/homtom-put` | `homtom-put` itself; callers `homtom-draw`, `homtom-touch-run`, `homtom-screen` |
| FTP 21 directly (curl or urllib) | `homtom-push`, `homtom-flash-bootloader`, `homtom-stage-live`, `homtom-touch-run` (results), `homtom-screen` (frames, live view) |
| Port probes | `homtom-state` (LINUX means 2323 answers; used by `homtom-wait`, `homtom-recover`, `homtom-to-storage`), `homtom-linklog`, `homtom-watch` (23 and 21) |
| Development bridge | A private development bridge uses the telnet console and `ftplib`; it is not part of this repository. |
| Device | `init.sh` listeners and `NETMSG`; `userland/homtom-shell`; `workspace/perf/boot-marks.sh` (`pidof telnetd tcpsvd`) |

Migration, one step at a time, each reversible:

1. **SSH alongside** (this work, once integrated). Nothing else changes.
2. **Transport switch on the Mac.** `tools/homtom-sh` gets a backend chosen by `HOMTOM_TRANSPORT=ssh|raw|auto` (auto: ssh when `ssh -o BatchMode=yes -o ConnectTimeout=3 homtom true` succeeds, else raw 2323). The ssh backend runs `ssh homtom "$1"` with stderr merged and the same 30 s cap; the wait argument becomes a timeout instead of a sleep. Add `tools/homtom-get PATH` and make `homtom-put` use `ssh homtom "cat > PATH"`, with a `wc -c` size check. Move every direct `curl ftp://` and `urllib` FTP call (`homtom-push`, `homtom-flash-bootloader`, `homtom-stage-live`, `homtom-touch-run`, `homtom-screen`) to those two helpers. `homtom-state` reports `ssh22=` and treats 22 or 2323 as LINUX; `homtom-linklog` and `homtom-watch` probe 22 too. The connection-sharing settings in section 4 keep repeated calls (`homtom-tap`, `homtom-ha-bridge.py`, the `homtom-screen` live view) from paying a key exchange each time. Measure large uploads (`homtom-push`, several MB) over SSH against FTP before relying on it.
3. **Legacy listeners optional.** `init.sh` starts telnet, 2323 and FTP only when `INTERNAL:/homtom/legacy.off` is absent. Recovery when SSH breaks: storage mode, delete `legacy.off`, boot. Run like this for a while with all tools on `HOMTOM_TRANSPORT=ssh`.
4. **Legacy listeners removed.** Drop the three `sv` lines, `homtom-shell`, and `telnetd ftpd` from the applet list (`tcpsvd` stays while `homtom-status-legacy` uses it). Recovery is storage mode, as it already is for a broken image.

## 8. Open issues

1. **Never run on the ARM920T.** qemu's TI925T is an ARMv4T model, not this core; cache, MMU and timing differ. First device test: `dropbear -V`, `dropbearkey -t ed25519 -f /tmp/k` over the existing telnet shell before putting it in the boot path.
2. **Speed of the key exchange and login.** Ed25519 and X25519 in this server use compact, slow reference code, and ML-KEM-768 adds to it. On a 200 MHz ARM9 without a measurement, expect a login to take seconds, not milliseconds. Measure `time ssh homtom true` on the device; if it is too slow, `ControlMaster` (section 4) hides it for repeated calls, and dropping `DROPBEAR_MLKEM768` would bring back the OpenSSH 10 warning.
3. **Entropy at first boot.** `dropbearkey` uses `getrandom()`, which blocks until the kernel's random pool is initialised. Linux 6.1 fills it from timer jitter when a caller blocks, so this should take about a second, but it is not measured on the device; the 10 s start delay in the proposed `init.sh` line helps.
4. **Throughput.** `chacha20-poly1305` on the ARM9 for multi-megabyte uploads (`homtom-push`) is not measured; compare with FTP before step 2 of the migration.
5. **Memory.** A connected session costs one forked server plus a shell; not measured against the device's free RAM with the GUI and Bluetooth running.
6. **Fallback address.** SSH does not answer on `169.254.10.10`. Add a second `-p 169.254.10.10:22` to the launcher if the Mac ever needs the link-local fallback for SSH.
7. **Revocation while running.** The key list is copied to RAM at each (re)start. Removing a key on INTERNAL from a shell takes effect at the next dropbear restart or boot; to cut access at once also delete `/root/.ssh/authorized_keys` and kill dropbear.
8. **Placement of the binary.** The proposal copies it to `tools/devtools/dropbearmulti` like the other device binaries (committed). That is 223 kB in the repository; alternatively the image build can take it from `artifacts/toolchain-armv4t/dropbear/` and check it against the hash above.
