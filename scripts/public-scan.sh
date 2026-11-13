#!/bin/bash
# Scan an exported tree for personal data and secrets. Prints file:line: [kind] match.
# Usage: scripts/public-scan.sh OUTDIR
# Optional env: PUBLIC_SCAN_EXTRA = extra regex (maintainer-specific hostnames, names, ids);
#               PUBLIC_EMAIL     = neutral address that is allowed to appear.
# Exit status: 0 clean, 1 findings, 2 usage error.
set -eu

DIR=${1:-}
[ -d "$DIR" ] || { echo "usage: $0 OUTDIR" >&2; exit 2; }
cd "$DIR"

find . -path ./.git -prune -o -type f -print0 | PUBLIC_EMAIL="${PUBLIC_EMAIL:-}" EXTRA="${PUBLIC_SCAN_EXTRA:-}" perl -0ne '
	BEGIN {
		$| = 1;
		@rules = (
			["email",      qr/[A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+(?:\.[A-Za-z0-9\-]+)*\.[A-Za-z]{2,}/, sub { $_[0] eq $ENV{PUBLIC_EMAIL} || $_[0] =~ /^(noreply|no-reply)@/ }],
			["home-path",  qr{(?:/Users/|/home/)[A-Za-z0-9._\-]+|~/dev/(?:pvt|work|personal)\b}, sub { $_[0] =~ m{^/(Users|home)/(user|you|name|root|runner|\$|USER|<)} }],
			["ipv4",       qr/(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.]*\d)(?!\.\d)/, sub {
				my $ip = $_[0];
				return 1 if $ip =~ /^192\.168\.1\.\d+$/ || $ip =~ /^169\.254\.\d+\.\d+$/;
				return 1 if $ip =~ /^(?:127\.0\.0\.1|0\.0\.0\.0|255\.\d+\.\d+\.\d+)$/;
				return 1 unless !grep { $_ > 255 } split /\./, $ip;
				return 0 }],
			["mac",        qr/(?<![0-9A-Fa-f:\-])(?:[0-9A-Fa-f]{2}[:\-]){5}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:\-])/, sub { $_[0] =~ /^02:54:54:/i }],
			["bd_addr",    qr/bd_?addr(?:ess)?\W{0,12}[0-9A-Fa-f]{2}(?:[:\- ]?[0-9A-Fa-f]{2}){2,5}|\bBD_ADDR\b[^\n]*[0-9A-Fa-f]{12}/i],
			["serial",     qr/(?:serial(?:\s*(?:no|number|nr))?|s\/n|imei|\bsn)\s*[:=#]\s*["\x27]?[A-Za-z0-9\-]{6,}|\b[A-Z]{2}\d{8,}\b/i, sub { $_[0] =~ /^serial\s*[:=]\s*["\x27]?(?:none|unknown|null|0+|%s|\$)/i }],
			["webhook",    qr{/api/webhook/[A-Za-z0-9_\-]{6,}|webhook[_\-]?id\W{0,6}[A-Za-z0-9_\-]{12,}}i, sub { $_[0] =~ /webhook\/(?:ID|<|\$|\{|%s|YOUR|xxx)/i }],
			["ha-host",    qr/(?:homeassistant|hassio|hass|ha)(?:[\-\w]*)\.(?:local|lan|home|internal)\b|[\w\-]+\.(?:ts\.net|duckdns\.org|nabu\.casa|ui\.nabu\.casa)\b/i],
			["pin",        qr/\bpin(?:_?code)?\s*[=:]\s*["\x27]?\d{4,8}\b|\bPIN\b[^\n]{0,20}\b\d{4,8}\b/i],
			["token",      qr/gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|\bAKIA[0-9A-Z]{16}\b|eyJ[A-Za-z0-9_\-]{15,}\.[A-Za-z0-9_\-]{10,}|-----BEGIN [A-Z ]*PRIVATE KEY-----|xox[abp]-[A-Za-z0-9\-]{10,}|(?:api[_\-]?key|secret|token|password|passwd)\s*[=:]\s*["\x27][^"\x27\s]{12,}["\x27]/i],
			["hex-key",    qr/(?<![0-9A-Fa-f])[0-9A-Fa-f]{33,}(?![0-9A-Fa-f])/, sub { 0 }],
		);
		push @rules, ["extra", qr/$ENV{EXTRA}/i] if length $ENV{EXTRA};
		$found = 0;
	}
	chomp; my $f = $_;
	if ($f =~ m{(?:^|/)(?:bt\.pin|btkeys|ha_webhook|\.env|id_rsa|[^/]*\.pem|[^/]*\.key)$}) { (my $d = $f) =~ s{^\./}{}; print "$d:1: [secret-file] file name\n"; $found++; }
	next unless -f $f && -s $f && !-B $f;
	(my $disp = $f) =~ s{^\./}{};
	open(my $fh, "<", $f) or next; local $/ = "\n";
	while (my $line = <$fh>) {
		chomp $line;
		for my $r (@rules) {
			my ($kind, $re, $allow) = @$r;
			while ($line =~ /($re)/g) {
				my $m = $1;
				next if $allow && $allow->($m);
				next if $kind eq "hex-key" && $line =~ /sha(?:256|512|1)|checksum|digest|hash|\.fb\b|0x[0-9a-f]/i;
				print "$disp:$.: [$kind] $m\n";
				$found++;
			}
		}
	}
	close $fh;
	END { print STDERR "public-scan: $found finding(s)\n"; exit($found ? 1 : 0) }
' 
