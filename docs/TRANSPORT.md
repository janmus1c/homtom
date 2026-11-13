# Transport

The initial passive macOS enumeration returned an empty SPUSBDataType list and no positively identified TomTom USB interface. This means no device was visible through those reads; it does not prove the cable is disconnected or the device is faulty. Full raw outputs are local artifacts.

The user subsequently captured `USB Vendor Name = TomTom`, `USB Product Name = GO 630`, `idVendor = 5008` (`0x1390`) and `idProduct = 1` (`0x0001`) in their Terminal. A contemporaneous read from the setup process still returned controllers only. Preserve this visibility discrepancy; do not infer physical disconnection. Run `./scripts/host-discovery.sh` from the user's Terminal to capture descriptors and the read-only inventory from that execution context. This changes no macOS settings. Restarting the project supervisor from that Terminal may be necessary for it to inherit the same visibility; the cause has not been established.

`device_probe` saves USB topology, hardware ports, ifconfig and external-disk data. It does not configure networking, mount storage, send packets to guessed IPs, or reboot the device.

OpenTom documentation mentions root Telnet/FTP on historical addresses `192.168.1.10` and `192.168.1.200`; both are unverified hypotheses. Before enabling a Telnet adapter, a human must identify the actual USB interface, its address and route, and record the link as verified only with evidence. The bridge checks the live route against the dedicated configured interface for each operation and rejects Wi-Fi/VPN routing. Telnet accepts named fixed read-only queries, never arbitrary shell text. Password login is not implemented. FTP retrieval is limited to two known `ttsystem` paths, uses the verified peer, and has byte/time budgets.

No mounted-storage adapter is active. No RNDIS kernel extension or VM is installed. If the actual USB mode requires Linux passthrough, record the descriptor evidence and prepare a narrowly scoped VM later. UART capture requires measured voltage and a verified pinout; no serial adapter is present in this implementation.
