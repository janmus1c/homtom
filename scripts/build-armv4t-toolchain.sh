#!/bin/bash
# Build a static ARMv4T (ARM920T) uClibc-ng toolchain and a static key-only SSH server for the device, and verify both.
#
#   scripts/build-armv4t-toolchain.sh [all|toolchain|dropbear|verify|launcher|hostssh]   (default: all)
#
# Everything runs in a pinned debian:bookworm container (native platform); the device is never contacted.
# Outputs, all under artifacts/toolchain-armv4t/ (ignored by git):
#   dl/                         source tarball cache, checked against the hashes below
#   armv4t-toolchain.tar.xz     the toolchain, unpacks to /opt/armv4t (BR2_HOST_DIR), with buildroot.config next to it
#   dropbear/dropbearmulti      stripped static server + key generator (install as /bin/dropbearmulti, links dropbear and dropbearkey)
#   dropbear/dropbearmulti.debug, probe, probe-v5te-control, SHA256SUMS, BUILDINFO
#   verify.log, launcher.log, hostssh.log
# A stage reuses the outputs of the earlier ones; delete armv4t-toolchain.tar.xz to rebuild the toolchain (about 20 min).
# Test keys are made in a temporary directory inside the container (or mktemp -d on the host) and removed afterwards.
set -euo pipefail

BR_VERSION=2026.02.3
BR_SHA256=5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb
DB_VERSION=2026.94
DB_SHA256=e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
IMAGE=debian:bookworm@sha256:268fb24c39bca92d326dd83ea3abebe4a3f7751c804ffef23bb6d857dd343ff8
TUPLE=arm-buildroot-linux-uclibcgnueabi
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1767225600}
QCPU=ti925t	# the only ARMv4T core qemu models (no ARM920T model); it lacks every ARMv5 instruction, so clz/blx/ldrd trap

inside() {
	export DEBIAN_FRONTEND=noninteractive SOURCE_DATE_EPOCH
	stage=$1
	apt_get() { apt-get update -qq && apt-get install -qq -y --no-install-recommends "$@" > /dev/null; }
	fetch() {	# fetch URL SHA256: into /dl, verified
		local f=/dl/${1##*/}
		if [ ! -f "$f" ]; then wget -q -O "$f.part" "$1" && mv "$f.part" "$f"; fi
		echo "$2  $f" | sha256sum -c --quiet - || { echo "hash mismatch: $f" >&2; exit 1; }
	}
	untool() { [ -x /opt/armv4t/bin/$TUPLE-gcc ] || tar -C /opt -xJf /out/armv4t-toolchain.tar.xz; export PATH=/opt/armv4t/bin:$PATH; }

	case $stage in
	toolchain)
		apt_get build-essential ca-certificates wget cpio unzip rsync bc file python3 perl xz-utils bzip2 patch
		fetch https://buildroot.org/downloads/buildroot-$BR_VERSION.tar.xz $BR_SHA256
		mkdir -p /build && tar -C /build -xJf /dl/buildroot-$BR_VERSION.tar.xz
		cd /build/buildroot-$BR_VERSION
		cp /cfg/buildroot.defconfig configs/homtom_armv4t_defconfig
		export FORCE_UNSAFE_CONFIGURE=1
		make -s homtom_armv4t_defconfig
		# the fragment must survive olddefconfig unchanged (a dropped symbol means an unsupported combination)
		while IFS= read -r l; do
			case "$l" in BR2_*=*) grep -qxF "$l" .config || { echo "defconfig line not applied: $l" >&2; exit 1; } ;; esac
		done < /cfg/buildroot.defconfig
		make toolchain > /tmp/br.log 2>&1 || { tail -n 60 /tmp/br.log; exit 1; }
		cp .config /out/buildroot.config
		tar -C /opt --sort=name --mtime=@"$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner -cJf /out/armv4t-toolchain.tar.xz.part armv4t
		mv /out/armv4t-toolchain.tar.xz.part /out/armv4t-toolchain.tar.xz
		/opt/armv4t/bin/$TUPLE-gcc -v 2>&1 | tail -n 1
		;;
	dropbear)
		apt_get build-essential ca-certificates wget bzip2 xz-utils file
		untool
		fetch https://matt.ucc.asn.au/dropbear/releases/dropbear-$DB_VERSION.tar.bz2 $DB_SHA256
		mkdir -p /build && tar -C /build -xjf /dl/dropbear-$DB_VERSION.tar.bz2
		cd /build/dropbear-$DB_VERSION
		cp /cfg/dropbear-localoptions.h localoptions.h
		flags="-Os -marm -march=armv4t -mtune=arm920t -ffunction-sections -fdata-sections -ffile-prefix-map=$PWD=."
		./configure -q --host=$TUPLE --enable-static --enable-bundled-libtom --disable-zlib --disable-lastlog \
			--disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx --disable-loginfunc --disable-pututline \
			--disable-pututxline CFLAGS="$flags" LDFLAGS="-Wl,--gc-sections -Wl,--build-id=none"
		make -s -j"$(nproc)" PROGRAMS="dropbear dropbearkey" MULTI=1 STATIC=1 > /tmp/db.log 2>&1 || { tail -n 40 /tmp/db.log; exit 1; }
		mkdir -p /out/dropbear
		cp dropbearmulti /out/dropbear/dropbearmulti.debug
		$TUPLE-strip -s -R .comment -R .note -o /out/dropbear/dropbearmulti dropbearmulti
		$TUPLE-gcc -static -Os -o /out/dropbear/probe /cfg/probe.c
		$TUPLE-gcc -static -Os -march=armv5te -DUSE_CLZ -o /out/dropbear/probe-v5te-control /cfg/probe.c
		{
			echo "dropbear $DB_VERSION (sha256 $DB_SHA256)"
			echo "buildroot $BR_VERSION (sha256 $BR_SHA256)"
			echo "image $IMAGE"
			echo "compiler: $($TUPLE-gcc --version | head -n 1)"
			echo "cflags: $flags"
			echo "programs: dropbear dropbearkey (MULTI=1 STATIC=1)"
		} > /out/dropbear/BUILDINFO
		cd /out/dropbear && sha256sum dropbearmulti dropbearmulti.debug probe probe-v5te-control > SHA256SUMS
		ls -l /out/dropbear
		;;
	verify)
		apt_get qemu-user openssh-client xz-utils
		untool
		set +e +o pipefail
		fail=0
		bad() { echo "FAIL: $*"; fail=1; }
		v5plus='^(clz|blx|ldrd|strd|pld|pli|bkpt|ldc2|stc2|mcr2|mrc2|cdp2|mcrr|mrrc|smla[bwt]|smul[bwt]|smlal[bt]|smua|smus|smls|smml|smmu|qadd|qsub|qdadd|qdsub|ldrex|strex|clrex|rev|sxt|uxt|sel|pkh|ssat|usat|umaal|sadd|ssub|uadd|usub|shadd|shsub|uhadd|uhsub|uqadd|uqsub|usad|cps|srs|rfe|setend|wfe|wfi|sev|yield|movw|movt|dmb|dsb|isb|ubfx|sbfx|bfi|bfc|rbit|mls|sdiv|udiv|cbz|cbnz|it|v[a-z]+)'
		for f in dropbearmulti.debug probe; do
			p=/out/dropbear/$f
			echo "== $f"
			arch=$($TUPLE-readelf -A "$p" | sed -n 's/^ *Tag_CPU_arch: //p')
			echo "Tag_CPU_arch: $arch"
			[ "$arch" = v4T ] || bad "$f Tag_CPU_arch is '$arch', expected v4T"
			$TUPLE-readelf -A "$p" | grep -E 'Tag_(THUMB_ISA_use|ARM_ISA_use|CPU_name)'
			$TUPLE-readelf -h "$p" | grep -E 'Flags|Type'
			$TUPLE-readelf -l "$p" | grep -q INTERP && bad "$f is dynamically linked"
			# mnemonic column of the disassembly (mapping symbols keep literal pools out); conditional suffixes allowed.
			# libgcc's unwinder carries VFP save/restore helpers that run only for frames holding VFP registers, which
			# soft-float code never has; they are listed but not counted.
			vfp=$($TUPLE-objdump -d "$p" | grep -oE '<__gnu_Unwind_(Save|Restore)_VFP[^>]*>' | sort -u | tr '\n' ' ')
			[ -z "$vfp" ] || echo "allowed (VFP-only unwinder helpers, never called): $vfp"
			hits=$($TUPLE-objdump -d --no-show-raw-insn "$p" | awk -F'\t' '/^[0-9a-f]+ <.*>:$/ {fn = $0; next}
				NF >= 2 && $1 ~ /^ *[0-9a-f]+:$/ && fn !~ /__gnu_Unwind_(Save|Restore)_VFP/ {print $2}' |
				grep -E "$v5plus" | sort | uniq -c)
			[ -z "$hits" ] || bad "$f has ARMv5+ instructions: $hits"
			$TUPLE-objdump -d "$p" | grep -E 'mrc.*cr13|mcr.*cr13' && bad "$f reads the v6K TLS register"
			[ "$($TUPLE-objdump -d --no-show-raw-insn "$p" | grep -cE '^ *[0-9a-f]+:'$'\t''[a-z]+\.(w|n)\b')" = 0 ] || bad "$f has Thumb-2 instructions"
			echo "instructions: $($TUPLE-objdump -d "$p" | grep -cE '^ *[0-9a-f]+:'), none ARMv5 or later"
		done
		export QEMU_CPU=$QCPU
		echo "== qemu-arm -cpu $QCPU ($(qemu-arm --version | head -n 1))"
		cpus=$(qemu-arm -cpu help 2>&1); echo "$cpus" | grep -qw "$QCPU" || bad "qemu has no $QCPU model"
		# explicit qemu-arm, not the container host's binfmt handler, so the CPU model and qemu version are the ones shown
		Q="qemu-arm -cpu $QCPU"
		$Q /out/dropbear/probe; rc=$?; echo "probe rc=$rc"; [ $rc = 0 ] || bad "probe failed under $QCPU"
		$Q /out/dropbear/probe-v5te-control > /dev/null 2>&1; rc=$?
		echo "v5te control rc=$rc (132 = SIGILL expected)"; [ $rc = 132 ] || bad "control did not trap: the CPU model accepts ARMv5"
		D=/out/dropbear/dropbearmulti
		$Q $D dropbear -V 2>&1; [ $? = 0 ] || bad "dropbear -V"
		# under /root: dropbear walks up from authorized_keys and rejects any group- or world-writable directory (/tmp)
		T=$(mktemp -d /root/verify.XXXXXX); trap 'rm -rf "$T"' EXIT
		printf '#!/bin/sh\nexec %s %s dropbear "$@"\n' "$Q" $D > $T/dropbear
		printf '#!/bin/sh\nexec %s %s dropbearkey "$@"\n' "$Q" $D > $T/dropbearkey
		chmod 755 $T/dropbear $T/dropbearkey
		start=$(env -u SOURCE_DATE_EPOCH date +%s%N)
		$T/dropbearkey -t ed25519 -f $T/hostkey > $T/kg.out 2>&1 || bad "dropbearkey ed25519"
		echo "dropbearkey -t ed25519: $(( ($(env -u SOURCE_DATE_EPOCH date +%s%N) - start) / 1000000 )) ms under qemu"
		grep -E '^(Public key|Fingerprint)' $T/kg.out | sed 's/ssh-ed25519 .*/ssh-ed25519 <test key>/'
		$T/dropbearkey -t rsa -f $T/rsa > /dev/null 2>&1 && bad "rsa key type should be compiled out"
		$T/dropbearkey -y -f $T/hostkey | grep '^ssh-ed25519' | awk '{print "[127.0.0.1]:2222", $1, $2}' > $T/known_hosts
		ssh-keygen -q -t ed25519 -N '' -C test -f $T/client
		ssh-keygen -q -t ed25519 -N '' -C other -f $T/other
		mkdir -m 700 $T/auth; cp $T/client.pub $T/auth/authorized_keys; chmod 600 $T/auth/authorized_keys
		$T/dropbear -F -E -s -g -j -k -T 3 -p 127.0.0.1:2222 -r $T/hostkey -D $T/auth -P $T/pid > $T/server.log 2>&1 &
		srv=$!
		sleep 3
		o="-p 2222 -o BatchMode=yes -o UserKnownHostsFile=$T/known_hosts -o StrictHostKeyChecking=yes -o ConnectTimeout=20"
		out=$(ssh $o -i $T/client -o IdentitiesOnly=yes -v root@127.0.0.1 'echo SSH-OK; id -u' 2> $T/client.err)
		echo "$out"; echo "$out" | grep -q SSH-OK || { bad "key login"; grep -vE '^debug1: (Reading|/|Connecting|Local|Remote|compat|SSH2|kex: (client|server)|load|identity|expecting|SSH_MSG|rekey|sending|receiving|Server host|Host |Will|channel|Requesting|Entering|pledge|client_|Sending env)' $T/client.err | tail -n 15; }
		grep -E 'kex: algorithm|kex: server->client|Authentications that can continue' $T/client.err | sort -u
		grep 'Authentications that can continue:' $T/client.err | tr -d '\r' | grep -qv 'continue: publickey$' && bad "server offers more than publickey"
		ssh $o -i $T/other -o IdentitiesOnly=yes root@127.0.0.1 true 2> /dev/null && bad "unknown key accepted"
		echo "unknown key: rejected"
		ssh $o -o PubkeyAuthentication=no -o PreferredAuthentications=password,keyboard-interactive root@127.0.0.1 true 2> /dev/null &&
			bad "password login accepted"
		echo "password/keyboard-interactive: rejected"
		ssh $o -i $T/client -o IdentitiesOnly=yes -W 127.0.0.1:2222 root@127.0.0.1 < /dev/null > /dev/null 2> $T/fwd.err &&
			bad "TCP forwarding accepted"
		echo "TCP forwarding (-W): refused ($(grep -o 'open failed.*' $T/fwd.err | head -n 1))"
		kill $srv; wait $srv 2> /dev/null
		sed 's/^/server: /' $T/server.log | tail -n 12
		[ $fail = 0 ] && echo "VERIFY: PASS" || echo "VERIFY: FAIL"
		exit $fail
		;;
	launcher)
		# userland/homtom-sshd with only the device's BusyBox applets on PATH, the real paths it uses on the device, and
		# 192.168.1.10 on the loopback interface of this throwaway container.
		apt_get busybox-static openssh-client iproute2 qemu-user procps
		set +e +o pipefail
		fail=0
		bad() { echo "FAIL: $*"; fail=1; }
		ip addr add 192.168.1.10/32 dev lo || bad "cannot add 192.168.1.10 (needs --cap-add NET_ADMIN)"
		mkdir -p /dev-bin
		for a in $(cat /src/kernel/scripts/busybox-applets.txt); do ln -sf /bin/busybox /dev-bin/$a; done
		cp /src/userland/homtom-lock /src/userland/homtom-sshd /dev-bin/; chmod 755 /dev-bin/homtom-*
		# the device runs dropbearmulti directly; here each call goes through qemu-arm with the ARMv4T model
		printf '#!/bin/sh\nexec /usr/bin/qemu-arm -cpu %s /out/dropbear/dropbearmulti dropbear "$@"\n' $QCPU > /dev-bin/dropbear
		printf '#!/bin/sh\nexec /usr/bin/qemu-arm -cpu %s /out/dropbear/dropbearmulti dropbearkey "$@"\n' $QCPU > /dev-bin/dropbearkey
		chmod 755 /dev-bin/dropbear /dev-bin/dropbearkey
		T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
		ssh-keygen -q -t ed25519 -N '' -C test -f $T/client
		cp /etc/passwd $T/passwd.host
		rm -f /etc/passwd /etc/shells
		mkdir -p /mnt/internal/homtom /var/run; touch /mnt/internal/ttsystem.lab2613
		run() { env -i PATH=/dev-bin HOME=/ /bin/busybox sh /dev-bin/homtom-sshd lo > /dev/null 2>&1 & }
		listening() { grep -qE ':0016 0{8}:0000 0A' /proc/net/tcp; }	# a LISTEN socket on port 22 (TIME_WAIT lines do not count)
		up() { i=0; while [ $i -lt 40 ]; do listening && return 0; sleep 0.5; i=$((i+1)); done; return 1; }
		o="-o BatchMode=yes -o UserKnownHostsFile=$T/kh -o StrictHostKeyChecking=yes -o IdentitiesOnly=yes -i $T/client -o ConnectTimeout=20"
		echo "== 1: no authorized_keys"
		run; sleep 4
		listening && bad "listening without authorized_keys"
		tail -n 1 /tmp/sshd.log
		echo "== 2: authorized_keys holds a private key"
		cp $T/client /mnt/internal/homtom/authorized_keys
		run; sleep 4
		listening && bad "listening with a private key in authorized_keys"
		tail -n 1 /tmp/sshd.log
		echo "== 3: CRLF file with a comment, a non-ed25519 line and an options line"
		{ printf '# owner keys\r\n'; printf 'ssh-rsa AAAAB3NzaC1yc2E= old\r\n'; printf 'command="x" %s\r\n' "$(cat $T/client.pub)"
		  printf '%s\r\n' "$(cat $T/client.pub)"; } > /mnt/internal/homtom/authorized_keys
		run; up || bad "not listening"
		sleep 1
		tail -n 3 /tmp/sshd.log
		grep -n . /etc/passwd /etc/shells
		ls -l /mnt/internal/homtom/ssh /etc/dropbear | sed 's/  */ /g'
		awk '{print "192.168.1.10", $1, $2}' /mnt/internal/homtom/ssh/ed25519_host_key.pub > $T/kh
		out=$(ssh $o root@192.168.1.10 'echo SSH-OK; id -u; echo $PATH' 2>&1); echo "$out"
		echo "$out" | grep -q SSH-OK || bad "login through the launcher"
		ss -ltn | grep ':22 ' | sed 's/  */ /g'
		ss -ltn | grep ':22 ' | grep -qE ' 192\.168\.1\.10(%lo)?:22 ' || bad "not bound to 192.168.1.10 only"
		echo "== 4: second launcher"
		run; sleep 3; tail -n 1 /tmp/sshd.log
		echo "== 5: dropbear killed, respawn"
		kill "$(cat /var/run/dropbear.pid)"; sleep 1
		up || bad "no respawn"; sleep 1
		ssh $o root@192.168.1.10 'echo SSH-OK' 2>&1 | grep -q SSH-OK || bad "login after respawn"
		grep -c 'restart' /tmp/sshd.log | sed 's/^/restarts logged: /'
		echo "== 6: reboot keeps the host key"
		fp1=$(cat /mnt/internal/homtom/ssh/fingerprint)
		pkill -f homtom-sshd; pkill -f dropbearmulti; sleep 1
		rm -rf /etc/dropbear /tmp/lock.sshd /root
		run; up || bad "not listening after reboot"; sleep 1
		[ "$fp1" = "$(cat /mnt/internal/homtom/ssh/fingerprint)" ] || bad "host key changed across reboot"
		ssh $o root@192.168.1.10 'echo SSH-OK' 2>&1 | grep -q SSH-OK || bad "pinned host key after reboot"
		echo "fingerprint stable: $fp1" | sed 's/SHA256:.*/SHA256:<test>/'
		echo "== 7: authorized_keys removed: no restart"
		rm /mnt/internal/homtom/authorized_keys
		kill "$(cat /var/run/dropbear.pid)"; sleep 8
		listening && bad "restarted without authorized_keys"
		tail -n 1 /tmp/sshd.log
		pkill -f homtom-sshd; pkill -f dropbearmulti
		[ $fail = 0 ] && echo "LAUNCHER: PASS" || echo "LAUNCHER: FAIL"
		exit $fail
		;;
	esac
}

if [ "${1:-}" = --inside ]; then
	shift
	inside "$@"
	exit
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/artifacts/toolchain-armv4t
STAGE=${1:-all}
mkdir -p "$OUT/dl"
run() {	# run STAGE [docker options...]
	local s=$1; shift
	docker run --rm "$@" -e SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
		-v "$ROOT/scripts/build-armv4t-toolchain.sh:/build.sh:ro" -v "$ROOT/build-env/armv4t:/cfg:ro" \
		-v "$OUT:/out" -v "$OUT/dl:/dl" "$IMAGE" bash /build.sh --inside "$s"
}
toolchain() {
	if [ -f "$OUT/armv4t-toolchain.tar.xz" ]; then echo "toolchain: reusing $OUT/armv4t-toolchain.tar.xz"; return; fi
	run toolchain
}
dropbear() { [ -f "$OUT/armv4t-toolchain.tar.xz" ] || toolchain; run dropbear; }
verify() { run verify 2>&1 | tee "$OUT/verify.log"; }
launcher() { run launcher --cap-add NET_ADMIN -v "$ROOT:/src:ro" 2>&1 | tee "$OUT/launcher.log"; }
hostssh() {
	# This Mac's OpenSSH against the ARMv4T server (qemu) in a container, published on 127.0.0.1 only: checks the
	# key exchange a current client negotiates. Throwaway keys in a host temp dir, removed on exit.
	local T port=22022 name=homtom-sshd-test-$$
	T=$(mktemp -d); trap 'docker rm -f "$name" > /dev/null 2>&1; rm -rf "$T"' RETURN
	ssh-keygen -q -t ed25519 -N '' -C test -f "$T/client"
	mkdir -m 700 "$T/auth"; cp "$T/client.pub" "$T/auth/authorized_keys"
	docker run -d --name "$name" -p 127.0.0.1:$port:22 -v "$OUT/dropbear:/d:ro" -v "$T/auth:/auth:ro" "$IMAGE" bash -c "
		apt-get update -qq && apt-get install -qq -y --no-install-recommends qemu-user > /dev/null
		mkdir -m 700 /a && cp /auth/authorized_keys /a/ && chmod 600 /a/authorized_keys
		Q='qemu-arm -cpu $QCPU /d/dropbearmulti'
		\$Q dropbearkey -t ed25519 -f /hk > /dev/null && \$Q dropbearkey -y -f /hk | grep ^ssh-ed25519 > /hk.pub
		exec \$Q dropbear -F -E -s -g -j -k -p 0.0.0.0:22 -r /hk -D /a" > /dev/null
	local i=0
	until docker exec "$name" test -f /hk.pub 2> /dev/null && nc -z 127.0.0.1 $port 2> /dev/null; do
		i=$((i + 1)); [ $i -lt 120 ] || { echo "hostssh: server did not start"; docker logs "$name" | tail; return 1; }; sleep 1
	done
	sleep 1
	docker exec "$name" cat /hk.pub | awk -v p=$port '{print "[127.0.0.1]:" p, $1, $2}' > "$T/kh"
	{
		echo "client: $(ssh -V 2>&1)"
		ssh -p $port -i "$T/client" -o IdentitiesOnly=yes -o BatchMode=yes -o UserKnownHostsFile="$T/kh" \
			-o StrictHostKeyChecking=yes -v root@127.0.0.1 'echo SSH-OK' 2> "$T/err"
		grep -E 'kex: algorithm|kex: server->client|host key algorithm|WARNING' "$T/err" | sort -u
	} | tee "$OUT/hostssh.log"
	grep -q SSH-OK "$OUT/hostssh.log"
}

case $STAGE in
toolchain|dropbear|verify|launcher|hostssh) $STAGE ;;
all) toolchain; dropbear; verify; launcher; hostssh ;;
*) echo "usage: $0 [all|toolchain|dropbear|verify|launcher|hostssh]" >&2; exit 2 ;;
esac
