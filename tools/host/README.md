# Mac-side helpers (Swift, build with `swiftc -O -o NAME NAME.swift`)
- `homtom-bt-pair.swift ADDR PIN`: pairs this Mac with the device (open the window first: `tools/homtom-sh 'touch /tmp/bt.pair'`; ADDR as `00-02-5b-00-a5-a5` for the CSR default chip address; PIN from `.runtime/bt_pin`, never committed).
- `homtom-bt-rfcomm.swift ADDR CHANNEL CMD...`: opens the RFCOMM serial channel (1) and sends command lines (`ping`, `status`, `help`, `quit`), printing the replies.
- `audio-rec.swift SECONDS FREQ`: records the Mac microphone and prints rms and the level at FREQ (used to verify the TomTom speaker; play tones with `afplay`). `audio-rec.swift SECONDS FREQ scan` also prints the strongest frequency within ±50 % of FREQ and its ratio to FREQ (a 1000 Hz test tone heard at 1000 Hz means the device plays at its nominal rate).
macOS may ask for Bluetooth/microphone permission for the host terminal on first use.
