# Security

## Reporting

Report vulnerabilities and leaked secrets privately through GitHub's "Report a
vulnerability" form on the Security tab of this repository. Do not open a public
issue for them.

Include the file or commit, what is exposed, and how you found it. Do not paste
the secret itself into the report.

## In scope

- A secret committed to the repository or a release asset: Bluetooth PIN or
  link key, token, webhook id, password, private key.
- Personal data in the tree or history: names, email addresses, hostnames, MAC
  addresses, serial numbers, home paths.
- Flaws in the host tools or in the device-side services that let another party
  reach the unit or the host.

## Known exposure by design

The USB link to the unit is unauthenticated: Telnet, a raw shell on port 2323
and anonymous FTP are open on it. Treat the link as trusted-host-only and do not
bridge it onto a network. Reports about this are welcome as hardening
suggestions, but they are not vulnerabilities in the current design.

## Response

This is a small volunteer project. Expect an acknowledgement, not a schedule.
If a secret has leaked, rotate it first, then report it.
