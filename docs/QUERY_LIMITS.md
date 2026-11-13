# What the current read-only queries establish

`partitions` reads `/proc/partitions`: major/minor device numbers, block counts and device names. It does not establish partition types, filesystem types, labels, UUIDs or a complete partition table. Those require a separately justified capture method.

`mounts` reads `/proc/mounts`: source, mount point, filesystem type and mount options. It does not report filesystem size, free space or the source of an initramfs image. A kernel `root=` argument does not establish the mounted root medium.

`init` lists init-related paths. It does not read their contents or establish the executed boot sequence. The current bridge has no arbitrary file-read command on the device.

Owner Terminal evidence and setup-process evidence come from distinct captures. `artifacts/owner-terminal-usb.txt` contains the owner's positive GO 630 descriptor output; `artifacts/usb-owner-followup.txt` is the setup process's contemporaneous negative capture. Do not attribute both to the owner's Terminal.

The initial storage uncertainty draft was reviewed against these limitations. Correct its query table while retaining that missing evidence remains missing. Passing an output validator establishes a fresh, nonempty, checksummed artifact and any declared structural requirements; it does not prove every scientific claim in that draft.
