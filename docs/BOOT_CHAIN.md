# Boot chain

The user reports `/proc/cmdline` contains `root=/dev/mmcblk0p2`, while `/` appears as `rootfs`. The boot argument does not establish which medium supplies the active root filesystem. An initramfs root remains a hypothesis.

Reported mounts: MTD `ngffs` at `/mnt/flash`, `/dev/mmcblk1` VFAT at `/mnt/sdcard`, `/dev/mmcblk0p1` VFAT at `/mnt/movinand`. The partition capture was clipped. A 512 KiB mtdblock0 and a 2 GB class mmcblk1 were visible.

Obtain complete procfs, mount and device inventories through named read-only bridge queries before selecting a backup method. Identify `ttsystem`, bootloader version, image origin, ARM OABI/EABI and actual boot priority. Do not reproduce a fork's build and assume equivalence with the installed image.
