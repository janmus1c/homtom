# homtom-btd: pairing and a serial channel over Bluetooth Classic

Status: draft, compile-checked only (2026-11-07). Nothing here has run on the device.

`homtom-btd.c` is a freestanding ARMv4T daemon. It lets a Mac pair with the BT 1.2 CSR BC4 using a legacy PIN. It publishes one Serial Port Profile (SPP) record over SDP and serves the homtom status JSON on RFCOMM channel 1. `hcictl-info.c` prints the kernel's view of `hci0`.

## Design

Linux 6.1 (checked in `.runtime/linux-6.1`, 6.1.191) already provides L2CAP (in `bluetooth.ko`) and RFCOMM (in `rfcomm.ko`). The missing parts are what BlueZ's `bluetoothd` would normally do: pairing replies, the link-key store and SDP. The daemon covers those three and nothing else.

| Job | Who does it | Why |
|---|---|---|
| Accept incoming ACL | kernel | `hci_conn_request_evt` accepts when an L2CAP listener exists (our PSM 1 socket qualifies) and rejects otherwise. The daemon must not send Accept_Connection_Request (0x0409) as well. |
| PIN Code Request (0x16) | daemon, raw HCI socket | Without `HCI_MGMT` the kernel sends nothing. It only sends a negative reply when `HCI_BONDABLE` is clear, and HCIDEVUP via ioctl sets `HCI_BONDABLE` (`hci_core.c` around line 498). |
| Link Key Request (0x17) | daemon | `hci_link_key_request_evt` returns at once when `HCI_MGMT` is clear. |
| Link Key Notification (0x18) | daemon stores; kernel records the key type | `hci_link_key_notify_evt` calls `conn_set_key` before its `HCI_MGMT` check. |
| Forcing authentication | controller, Write_Authentication_Enable = 1 (security mode 3) | Every ACL link must authenticate before Connection Complete, so an unpaired peer cannot even reach SDP. The kernel then marks each connection `HCI_CONN_AUTH` (`hci_event.c` line 3176). |
| Encryption | kernel, per socket: RFCOMM `BT_SECURITY` MEDIUM | MEDIUM makes the kernel request authentication if needed, then Set_Connection_Encryption. A BT 1.2 controller has no Read_Encryption_Key_Size, so the kernel assumes 16. |
| Write_Encryption_Mode | **not used** | It sets `HCI_CONN_ENCRYPT` at Connection Complete with `enc_key_size` 0, and `l2cap_check_enc_key_size` then blocks every L2CAP channel. |
| Write_Simple_Pairing_Mode | **not used** | Simple pairing arrived in BT 2.1; the BC4 is BT 1.2 and has no such command. |
| Role switch | kernel default | It accepts as peripheral; the Mac usually asks to become central. |
| SDP | daemon, L2CAP PSM 1 | The kernel has no SDP server. One record: 0x1101 SerialPort, L2CAP + RFCOMM channel 1, PublicBrowseGroup, profile 0x1101 v1.2, name "homtom status". It handles ServiceSearch, ServiceAttribute and ServiceSearchAttribute, accepts 16-, 32- and 128-bit UUIDs and supports continuation (host-tested with crafted PDUs). |
| Data | kernel RFCOMM socket, channel 1 | On connect the daemon sends one JSON line. It then answers `status`, `ping`, `help` and `quit`, one per line. JSON is the body line of `/bin/homtom-status` run with stdin `/dev/null` (the same JSON `:8080` serves). |

The mgmt interface was rejected because using it means writing a bluetoothd. Once a mgmt socket is opened, `HCI_MGMT` is set and `HCI_BONDABLE` is cleared. The kernel then routes PIN and key events to mgmt, and the daemon would have to power the device, set it connectable and bondable, and load keys through mgmt commands. **Never open a mgmt (HCI_CHANNEL_CONTROL) socket on this device.** Doing so silently disables the raw-socket pairing path until reboot.

### Files on the device

- `/mnt/internal/homtom/bt.pin`: the PIN (1 to 16 characters). If it is missing, every pairing is refused. Do not commit it.
- `/tmp/bt.pair`: the pairing window. If it is missing, new pairings are refused but already-paired peers still connect. The daemon deletes it after storing a key.
- `/mnt/internal/homtom/btkeys`: up to 8 records of 24 bytes (address, key type, PIN length, key). It is written to `btkeys.tmp`, fsynced, then renamed. To unpair everything, delete it.

## Build

The build runs in Docker (`debian:bookworm-slim` with `gcc-arm-linux-gnueabi`):

```
arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 -o homtom-btd homtom-btd.c
arm-linux-gnueabi-gcc -nostdlib -march=armv4t -marm -static -fno-stack-protector -O1 -o hcictl-info hcictl-info.c
```

Both builds pass `-Wall -Wextra` with no warnings. `nm -u` lists no undefined symbols and there are no `mem*` or `__aeabi_*` calls. homtom-btd has 12.6 KB of text.

## Is hci0 UP in the kernel?

- `hcictl-info 0` uses HCIGETDEVINFO and needs no controller traffic. It prints the flags (`UP` is bit 0; `PSCAN`/`ISCAN` reflect the last Write_Scan_Enable the kernel saw complete; `AUTH` reflects Write_Authentication_Enable), the counters `cmd_tx`/`evt_rx`/`err_rx`, and `acl_mtu`. It exits 0 only when the device is UP. To fold it into `hcictl info`, add the same body as an `argv[1][0] == 'i'` branch.
- Indirect evidence: in 6.1, `hci_sock_sendmsg` returns `-ENETDOWN` unless `HCI_UP` is set (`hci_sock.c` line 1799). hcictl ignores the send result, so the observation "events come back only after `hcictl up`" already suggests that UP is set. hcictl-info confirms it.
- sysfs: `/sys/class/bluetooth/hci0` only proves that the device is registered, not that it is UP.
- debugfs: `mount -t debugfs none /sys/kernel/debug`. Files such as `/sys/kernel/debug/bluetooth/hci0/features` and `hci_version` appear only after the kernel init sequence succeeded, and only if the kernel has `CONFIG_DEBUG_FS`; that is unverified.

## On-device test protocol

Prerequisites: an image carrying `homtom-btd` and `hcictl-info`, and BT brought up by `homtom-bt-start`. `build-ttsystem-6.1.sh` already ships `rfcomm.ko` in `/lib/modules`, but homtom-bt-start does not load it.

1. Check the state:
   ```
   hcictl-info 0 > /tmp/i 2>&1; cat /tmp/i
   ```
   Expect `UP PSCAN ISCAN`. Read the output via a file, because of the hciattach stdout issue (docs/LESSONS.md).
2. Load RFCOMM:
   ```
   insmod /lib/modules/rfcomm.ko
   ```
3. Set the PIN and open the pairing window:
   ```
   echo 0000 > /mnt/internal/homtom/bt.pin; touch /tmp/bt.pair
   ```
   Use a non-trivial PIN once the path works.
4. Start the daemon:
   ```
   homtom-btd > /tmp/btd.log 2>&1 < /dev/null &
   ```
   Then run `cat /tmp/btd.log`. Expect `hci0 ... flags ...` and `ready, ... auth on, rfcomm security medium`.
5. On the Mac, open System Settings > Bluetooth, find "homtom" and click Connect. When asked, enter the PIN. If macOS tries 0000 silently, a PIN of 0000 makes it succeed. Expect these log lines in order:
   - `conn request`
   - `link key request ... -> none`
   - `pin request ... -> replied`
   - `link key stored ... pairing window closed`
   - `conn complete status 00`
   - `sdp client`, then `sdp pdu 06 -> 07`
6. List the serial port on the Mac:
   ```
   ls /dev/cu.* | grep -i homtom
   ```
   The exact name, for example `/dev/cu.homtom` or `/dev/cu.homtom-homtomstatus`, is unverified.
7. Read the JSON (Python standard library only; opening the port makes macOS open RFCOMM):
   ```
   python3 -c "import os,termios,tty,time;p=[x for x in os.listdir('/dev') if x.startswith('cu.') and 'homtom' in x.lower()][0];f=os.open('/dev/'+p,os.O_RDWR|os.O_NOCTTY);tty.setraw(f);time.sleep(2);os.write(f,b'status\n');time.sleep(3);print(os.read(f,4096).decode())"
   ```
   On the device, expect `rfcomm client ...` in the log. The Mac should receive two JSON lines: the greeting and the reply.
8. Reconnect test: close the port, then repeat step 7. Expect `link key request ... -> stored key` and no PIN prompt.
9. Reboot test: reboot the device and repeat step 7. The stored key should be reused.

Isolation flags: `-l` drops RFCOMM to LOW, so the kernel does not request encryption. `-o` also leaves authentication off. Use them only to locate a failure, then return to the defaults.

## Failure modes to expect

- **No `conn request` at all**: hci0 is not UP, or Write_Scan_Enable was lost (`hcictl-info` will lack PSCAN). It could also be that the kernel rejected the connection because no listener existed: start the daemon before connecting. A daemon restart (sv) briefly leaves no listener.
- **`pin request ... refused`**: `/tmp/bt.pair` or `bt.pin` is missing.
- **PIN accepted but `auth complete status 05/06`**: the PIN or key does not match, or the Mac still holds an old key. Forget "homtom" on the Mac, delete `btkeys`, and pair again.
- **Kernel sends its own PIN negative reply** (two replies in the log or btmon): `HCI_MGMT` got set by something that opened a mgmt socket. Reboot.
- **`link key request ... -> stored key` followed by `auth complete status 06`**: the key file is stale (for example, the Mac was re-paired elsewhere). Delete the record.
- **Pairing OK but no `sdp client`**: the Mac does not see a usable service. Its class 0x280408 (Audio/Video) may make macOS look for audio profiles first. Try class 0x001f00 (uncategorised) or a Computer class in homtom-bt-discoverable.
- **SDP OK, no `/dev/cu.*`**: macOS did not register an SPP port. Check the SDP exchange in the log (pdu 06 -> 07 with len > 10). PacketLogger from Additional Tools for Xcode shows the SDP and RFCOMM traffic on the Mac side.
- **`rfcomm client` then an immediate close, or `encrypt change status` not 00**: a MEDIUM security problem on reconnect with a stored key. The kernel does not know the key type (0xff) and re-authenticates. Retry with `-l` to confirm, then look at kernel log lines from `hci_conn_security`.
- **`RFCOMM listen failed -93`**: `rfcomm.ko` is not loaded.
- **`hci socket error` / `hci recv -32`**: hci0 was unregistered because homtom-bt-start's health loop restarted the chip. The daemon exits, so it needs to run under `sv`. The new device may be `hci1`; check `hcictl-info 1` and start with `-d 1`.
- **`homtom-status timed out`**: the status script hung, for example on devmem. The client gets `{"error":"status unavailable"}`.
- **iPhone**: iOS can see "homtom" but offers no SPP to apps without MFi. Pairing may fail or be pointless, so use the Mac (or Android/Linux) as the host.
- **The serial line drops while the event loop runs the status script** (up to 5 s): this is acceptable for request/response use; the poll loop is single-threaded.

## Wiring later (not done)

- Add `rfcomm` to the insmod list in homtom-bt-start.
- After `homtom-bt-discoverable`, start `( sv homtom-btd ) &`.
- On the Mac side, the HA bridge reads `/dev/cu.*homtom*` instead of `http://192.168.1.10:8080/`.
