# Recovery gates

The TomTom's recoverable boot path is **unverified**. Discovery has no device write or reboot path.

Before implementing a removable-media write adapter, independently verify:

1. Complete storage mapping and exact removable-medium identity.
2. Checksummed backups, including boot image and relevant internal areas, with a safe read method and verified readability.
3. Actual boot priority and the tested known-good fallback procedure.
4. Console or observable boot outcome and a bounded recovery procedure.
5. An exact experiment manifest naming image hash, target, expected observable, time limit and recovery.

`write_removable`, `write_internal` and `power_control` are separate capability controls. Flags alone do not activate unimplemented operations. Keep them false until a separately reviewed adapter and evidence-backed phase gate exist. A USB disconnect or software reboot is not a battery-powered device power cycle. Never reboot the Mac or change its settings to recover the TomTom.
