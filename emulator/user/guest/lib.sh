# Helpers sourced by the emulator shims.
emu_log() {
	echo "`/bin/busybox cut -d' ' -f1 /proc/uptime 2>/dev/null` $*" >>/.emu/log/shim.log
}

emu_event() {
	echo "$*" >>/.emu/events
}

# Kernel module name of a .ko, as resolved by the supervisor from .gnu.linkonce.this_module.
emu_modname() {
	/bin/busybox sed -n "s|^$1 ||p" /.emu/modnames
}
