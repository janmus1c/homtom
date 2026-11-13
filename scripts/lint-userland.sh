#!/bin/sh
# Lint the device shell scripts against the stock BusyBox 1.22.1 (83 applets, ash). Mac-only, never touches the device.
# Inputs: every executable script in userland/ and the init.sh text embedded in kernel/scripts/build-ttsystem-6.1.sh.
# Usage: scripts/lint-userland.sh [--strict] [file ...]
#   --strict  exit 1 on CHECK findings too (default: only ERROR findings fail)
# Findings: "file:line: ERROR|CHECK [kind] text". Append "# lint-ok" to a script line to silence a reviewed finding.
# Knowledge base: kernel/scripts/busybox-applets.txt (applets), docs/LESSONS.md (grep -E, tail, tr, dd, printf, cmp,
# md5sum, fractional sleep are missing), docs/BUILD_HYGIENE.md (what was verified and how).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/lint-userland.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
STRICT=0
if [ "${1:-}" = --strict ]; then STRICT=1; shift; fi

if [ $# -gt 0 ]; then
	for f in "$@"; do echo "$f"; done > "$TMP/list"
else
	for f in "$ROOT"/userland/*; do
		[ -f "$f" ] || continue
		head -n 1 "$f" | grep -q -e '^#!' || continue
		echo "$f"
	done > "$TMP/list"
	sed -n "/^cat > \"\$ROOTFS\/init.sh\" << \"EOF\"\$/,/^EOF\$/p" "$ROOT/kernel/scripts/build-ttsystem-6.1.sh" \
		| sed '1d;$d' > "$TMP/init.sh"
	[ -s "$TMP/init.sh" ] || { echo "lint: could not extract init.sh from build-ttsystem-6.1.sh"; exit 2; }
	echo "$TMP/init.sh" >> "$TMP/list"
fi

# Commands that exist on the device besides applets: our own binaries and scripts.
{
	ls "$ROOT/userland" 2> /dev/null
	ls "$ROOT/tools/devtools" 2> /dev/null | sed -e 's/\.[ch]$//' -e '/\.sh$/d' -e '/^tests$/d'
	echo busybox; echo hello; echo init.sh
} | sort -u > "$TMP/shipped"

: > "$TMP/syntax"
while read -r f; do
	if ! sh -n "$f" 2> "$TMP/err"; then
		sed "s|^|$(basename "$f"): sh -n: |" "$TMP/err" >> "$TMP/syntax"
	fi
done < "$TMP/list"

python3 -I - "$ROOT/kernel/scripts/busybox-applets.txt" "$TMP/shipped" "$TMP/list" "$TMP/syntax" "$STRICT" <<'PY'
import re, sys

applets_file, shipped_file, list_file, syntax_file, strict = sys.argv[1:6]
applets = set(open(applets_file).read().split())
shipped = set(open(shipped_file).read().split())
files = [l.strip() for l in open(list_file) if l.strip()]

KEYWORDS = set("if then elif else fi for while until do done case esac in { } ! time".split())
# ash builtins that work regardless of the applet list (BusyBox 1.22.1 ash)
BUILTINS = set(". : break cd continue eval exec exit export getopts hash local read readonly return set shift "
               "trap type ulimit umask unset wait alias unalias command jobs fg bg".split())
BASHISM_CMDS = {
    "source": "use '.'", "function": "use name() { }", "declare": "no declare in ash", "typeset": "no typeset in ash",
    "let": "use $(( ))", "shopt": "bash only", "pushd": "bash only", "popd": "bash only", "mapfile": "bash only",
    "readarray": "bash only", "select": "bash only", "disown": "bash only", "compgen": "bash only",
}
WRAPPERS = {"sv", "exec", "command", "nice", "env", "nohup", "time", "watchdog_run"}

findings = []


def add(path, line, sev, kind, text):
    findings.append((path, line, sev, kind, text))


def name_of(p):
    return "init.sh(build-ttsystem-6.1.sh)" if p.endswith("/init.sh") and "lint-userland" in p else p.rsplit("/", 1)[-1]


def scan(path):
    src = open(path, errors="replace").read()
    lines = src.split("\n")
    nm = name_of(path)
    waived = {i + 1 for i, l in enumerate(lines) if "lint-ok" in l}
    funcs = set(re.findall(r"^\s*([A-Za-z_][A-Za-z0-9_-]*)\s*\(\)", src, re.M))

    def report(ln, sev, kind, text):
        if ln not in waived:
            add(nm, ln, sev, kind, text)

    # Pass 1: strip heredoc bodies, comments and quoted text, keeping $( ) contents of double quotes.
    out_lines = []          # (lineno, cleaned text), command positions marked by \x01
    pending = []            # heredoc tags
    i_ln = 0
    in_single = False
    while i_ln < len(lines):
        raw = lines[i_ln]
        ln = i_ln + 1
        i_ln += 1
        if pending:
            tag, strip = pending[0]
            if (raw.strip() if strip else raw) == tag:
                pending.pop(0)
            continue
        cleaned = []
        i = 0
        dq = False
        sub_depth = 0     # parentheses depth of $( ) opened inside double quotes
        stack = []
        n = len(raw)
        while i < n:
            c = raw[i]
            if in_single:
                if c == "'":
                    in_single = False
                i += 1
                continue
            if dq:
                if c == "\\":
                    i += 2
                    continue
                if c == '"' and sub_depth == 0:
                    dq = False
                    i += 1
                    continue
                if c == "$" and raw[i:i + 2] == "$(" and raw[i:i + 3] != "$((":
                    cleaned.append(" $( ")
                    sub_depth += 1
                    i += 2
                    continue
                if sub_depth and c == "(":
                    sub_depth += 1
                if sub_depth and c == ")":
                    sub_depth -= 1
                    cleaned.append(" ) ")
                    i += 1
                    continue
                if sub_depth:
                    if c == '"':
                        # quote inside $( ) inside "...": skip a nested quoted string
                        j = raw.find('"', i + 1)
                        i = (j + 1) if j >= 0 else n
                        continue
                    cleaned.append(c)
                i += 1
                continue
            if c == "\\":
                if i + 1 >= n:
                    cleaned.append("\x02")  # line continuation marker
                i += 2
                continue
            if c == "'":
                in_single = True
                cleaned.append("''")
                i += 1
                continue
            if c == '"':
                dq = True
                cleaned.append('""')
                i += 1
                continue
            if c == "#" and (i == 0 or raw[i - 1] in " \t;&|(){}"):
                break
            m = re.match(r"<<(-?)\s*(\"[^\"]+\"|'[^']+'|[A-Za-z_][A-Za-z0-9_]*)", raw[i:])
            if m and raw[i:i + 3] != "<<<":
                pending.append((m.group(2).strip("\"'"), m.group(1) == "-"))
                cleaned.append(" ")
                i += m.end()
                continue
            cleaned.append(c)
            i += 1
        out_lines.append((ln, "".join(cleaned)))

    # Pass 2: split into simple commands.
    stmt_ln = 0
    buf = ""
    stmts = []   # (lineno, text)
    for ln, text in out_lines:
        if not buf:
            stmt_ln = ln
        if text.endswith("\x02"):
            buf += text[:-1] + " "
            continue
        buf += text
        stmts.append((stmt_ln, buf))
        buf = ""
    if buf:
        stmts.append((stmt_ln, buf))

    def commands(text):
        """Yield (position-independent) argument lists of each simple command in text."""
        # case patterns: "a|b) cmd" at command position
        text = strip_arith(text)
        text = text.replace("$(", " ; ").replace("`", " ; ")
        text = re.sub(r"[0-9]*[<>]&[0-9-]*", " ", text)
        parts = re.split(r"&&|\|\||;;|;|\||&(?!>)|\(|\)|\{ | \}|\n", text)
        return parts

    for ln, text in stmts:
        # case pattern prefix: "pat|pat) rest"
        t = re.sub(r"^\s*case\b.*?\sin\b", " ", text)
        mp = re.match(r"^\s*\(?\s*[^\s;&|()<>]+(\s*\|\s*[^\s;&|()<>]+)*\s*\)(.*)$", t)
        if mp and not t.lstrip().startswith("$(") and re.match(r"^\s*(\(\s*)?[\w*?\[\]\-.\"'/:,+=@%~| ]+\)", t) \
                and not re.match(r"^\s*[A-Za-z_][A-Za-z0-9_]*\(\)", t):
            t = mp.group(2)
        for part in commands(t):
            words = part.split()
            # peel leading keywords, assignments and redirections
            while words:
                if words[0] in ("for", "case", "select"):
                    words = []
                elif words[0] in KEYWORDS:
                    words = words[1:]
                elif re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", words[0]):
                    words = words[1:]
                elif re.match(r"^[0-9]*[<>]+$", words[0]):
                    words = words[2:]
                elif re.match(r"^[0-9]*[<>]", words[0]):
                    words = words[1:]
                else:
                    break
            if not words:
                continue
            check_command(ln, words, funcs, report)
        check_text(ln, text, report)
        check_raw(ln, lines[ln - 1], report)


def strip_arith(text):
    out, i = [], 0
    while i < len(text):
        if text.startswith("$((", i):
            depth, j = 2, i + 3
            while j < len(text) and depth > 0:
                depth += (text[j] == "(") - (text[j] == ")")
                j += 1
            out.append(" ")
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def known(cmd, funcs):
    base = cmd.rsplit("/", 1)[-1] if "/" in cmd else cmd
    if cmd in funcs or base in funcs:
        return True
    return base in applets or base in shipped or (not "/" in cmd and cmd in BUILTINS)


def check_command(ln, words, funcs, report):
    cmd = words[0]
    if re.match(r"^[A-Za-z_][A-Za-z0-9_]*\(\)$", cmd) or cmd.endswith("()"):
        words = words[1:]
        if not words:
            return
        cmd = words[0]
        if cmd in KEYWORDS or cmd == "{":
            return
    if re.match(r"^__[A-Z]+__$", cmd):
        return
    if "$" in cmd or "*" in cmd or "?" in cmd or cmd.startswith(("-", "=", "\x03", "'", '"')) or cmd in ("''", '""'):
        return
    if cmd in BASHISM_CMDS:
        report(ln, "ERROR", "bashism", "%s: %s" % (cmd, BASHISM_CMDS[cmd]))
        return
    if cmd.startswith("/") and cmd.rsplit("/", 1)[0] not in ("/bin", "/sbin", "/usr/bin", "/usr/sbin"):
        return  # a script or file on INTERNAL / sysfs, not part of the image
    if cmd.startswith("./"):
        return
    base = cmd.rsplit("/", 1)[-1]
    if base == "busybox" and len(words) > 1:
        words = words[1:]
        cmd = words[0]
        base = cmd
        if cmd.startswith("-"):
            return
    if base in ("[", "[[", "test"):
        if base == "[[":
            report(ln, "CHECK", "bashism", "[[ ]] runs as the 'test' applet: no &&, ||, ==pattern or =~ inside")
        check_test(ln, words, report)
        return
    if base == "tcpsvd":
        w = [x for x in words[1:] if not x.startswith("-")]
        if len(w) >= 3:
            check_command(ln, w[2:], set(), report)
    if cmd in WRAPPERS or base in WRAPPERS:
        w = words[1:]
        while w and (w[0].startswith("-") or re.match(r"^[0-9]*[<>]+$", w[0])):
            w = w[2:] if re.match(r"^[0-9]*[<>]+$", w[0]) else w[1:]
        if w and not re.match(r"^[0-9]*[<>]", w[0]):
            check_command(ln, w, set(), report)
    if base == "grep":
        for a in words[1:]:
            if re.match(r"^-[a-zA-Z]*[EP][a-zA-Z]*$", a):
                report(ln, "ERROR", "grep-E", "grep %s unsupported on the stock BusyBox (use grep -e / sed)" % a)
            if a in ("-r", "-R"):
                report(ln, "CHECK", "grep-r", "grep -r not verified on the stock BusyBox")
    if base == "egrep":
        report(ln, "ERROR", "applet", "egrep is not an applet")
    if base == "sleep":
        for a in words[1:2]:
            if re.match(r"^[0-9]*\.[0-9]+$", a):
                report(ln, "ERROR", "sleep", "fractional sleep unsupported (docs/LESSONS.md)")
    if base == "read":
        for a in words[1:]:
            if re.match(r"^-[a-zA-Z]*[pnNtsd][a-zA-Z]*$", a):
                report(ln, "CHECK", "read-opt", "read %s: option support depends on the BusyBox build" % a)
    if base == "date" and any(a.startswith("-d") for a in words[1:]):
        report(ln, "CHECK", "date-d", "date -d: needs FEATURE_DATE_COMPAT, not verified")
    if base == "sed" and any(re.match(r"^-[a-zA-Z]*i", a) for a in words[1:]):
        report(ln, "CHECK", "sed-i", "sed -i: fine on 1.22.1 if FEATURE_SED is full, not verified")
    if base in ("trap",):
        for a in words[1:]:
            if a in ("ERR", "DEBUG", "RETURN"):
                report(ln, "ERROR", "bashism", "trap %s is bash only" % a)
    if base == "local" and False:
        pass
    if known(cmd, funcs):
        return
    if cmd.startswith("/"):
        report(ln, "ERROR", "applet", "%s not in the image (/bin has applets, homtom-* and tools/devtools binaries only)" % cmd)
    else:
        report(ln, "ERROR", "applet", "'%s' is not a BusyBox applet in busybox-applets.txt, a builtin or a shipped tool" % cmd)


def check_test(ln, words, report):
    for a in words[1:]:
        if a == "==":
            report(ln, "CHECK", "test-==", "'==' inside test: use '=' (portable)")
        if a == "=~":
            report(ln, "ERROR", "bashism", "'=~' is bash only")
        if a in ("-nt", "-ot", "-ef"):
            report(ln, "CHECK", "test-" + a, "test %s needs FEATURE_TEST_..., not verified" % a)
        if a in ("&&", "||") and words[0] == "[[":
            report(ln, "ERROR", "bashism", "%s inside [[ ]] is not understood by the test applet" % a)


def check_raw(ln, raw, report):
    """Expansions inside double quotes are blanked out of the cleaned text; look at the raw line (single-quoted text is data)."""
    r = re.sub(r"'[^']*'", "''", raw)
    if re.match(r"^\s*#", r):
        return
    if re.search(r"\$\{[A-Za-z_0-9]+(//|/#|/%|\^\^|,,)", r) or re.search(r"\$\{[A-Za-z_0-9]+/[^}]", r):
        report(ln, "CHECK", "param-subst", "${var/..} / case modification: ash needs ASH_BASH_COMPAT, not verifiable here")
    if re.search(r"\$\{[A-Za-z_0-9]+:[0-9$(]", r):
        report(ln, "CHECK", "param-subst", "${var:off:len}: needs ASH_BASH_COMPAT, not verifiable here")
    if re.search(r"\$\{[A-Za-z_]+\[|\$\{!", r):
        report(ln, "ERROR", "bashism", "array or indirect expansion")
    if re.search(r"\$(RANDOM|SECONDS|BASH_[A-Z]+|PIPESTATUS|EPOCHSECONDS)\b", r):
        report(ln, "CHECK", "bash-var", "bash-only or config-dependent variable")


def check_text(ln, text, report):
    if re.search(r"(^|[\s(;])[A-Za-z_][A-Za-z0-9_]*\+?=\(", text):
        report(ln, "ERROR", "bashism", "array assignment")
    if re.search(r"\$\{[A-Za-z_]+\[", text) or re.search(r"\$\{!", text) or re.search(r"\$\{#[A-Za-z_]+\[", text):
        report(ln, "ERROR", "bashism", "array or indirect expansion")
    if re.search(r"[<>]\(", text):
        report(ln, "ERROR", "bashism", "process substitution")
    if "<<<" in text:
        report(ln, "ERROR", "bashism", "here-string <<<")
    if re.search(r"&>|\|&", text):
        report(ln, "ERROR", "bashism", "&> or |& redirection (use > file 2>&1)")
    if re.search(r"\$'", text):
        report(ln, "ERROR", "bashism", "$'...' quoting")
    if re.search(r"\$\{[A-Za-z_0-9]+(//|/#|/%|\^\^|,,|\^|,)", text) or re.search(r"\$\{[A-Za-z_0-9]+/[^}]", text):
        report(ln, "CHECK", "param-subst", "${var/..} / case modification: ash needs FEATURE ASH_BASH_COMPAT, not verifiable here")
    if re.search(r"\$\{[A-Za-z_0-9]+:[0-9$(]", text):
        report(ln, "CHECK", "param-subst", "${var:off:len}: needs ASH_BASH_COMPAT, not verifiable here")
    if re.search(r"\$(RANDOM|SECONDS|BASH_[A-Z]+|LINENO|PIPESTATUS|EPOCHSECONDS)\b", text):
        report(ln, "CHECK", "bash-var", "bash-only or config-dependent variable")
    if re.search(r"\{[0-9a-z]+\.\.[0-9a-z]+\}", text):
        report(ln, "ERROR", "bashism", "brace expansion")
    if re.search(r"\bfunction\s+[A-Za-z_]", text):
        report(ln, "ERROR", "bashism", "'function name' syntax")
    if re.search(r"\bfor\s*\(\(", text) or re.search(r"(^|[;\s])\(\([^)]*\)\)\s*($|;)", text):
        report(ln, "ERROR", "bashism", "(( )) arithmetic command or for ((;;))")
    if "**" in text and "$((" in text:
        report(ln, "CHECK", "arith", "** in arithmetic: not supported by ash")
    if re.search(r"\+=", text) and not re.search(r"\$\(\(|\(\(", text):
        report(ln, "CHECK", "plus-assign", "'+=' assignment: bash only outside $(( ))")


for f in files:
    scan(f)

errs = [x for x in findings if x[2] == "ERROR"]
chks = [x for x in findings if x[2] == "CHECK"]
seen = set()
for path, ln, sev, kind, text in sorted(findings, key=lambda x: (x[0], x[1], x[2], x[3])):
    key = (path, ln, sev, kind, text)
    if key in seen:
        continue
    seen.add(key)
    print("%s:%d: %s [%s] %s" % (path, ln, sev, kind, text))
syn = [l.rstrip("\n") for l in open(syntax_file) if l.strip()]
for l in syn:
    print("%s ERROR [syntax]" % l)
print("lint-userland: %d files, %d ERROR, %d CHECK, %d syntax" % (len(files), len(errs), len(chks), len(syn)))
print("lint-userland: $(( )) arithmetic and 'local' are available in the project ash (strings of the 1.22.1 binary); "
      "${var//} and ${var:o:l} depend on ASH_BASH_COMPAT and are only flagged")
sys.exit(1 if errs or syn or (strict == "1" and chks) else 0)
PY
