/* homtom SSH server: key-only, root-only, server and key generator only (docs/SSH.md).
 * Overrides src/default_options.h; anything not listed keeps the upstream default. */

/* Standalone daemon only, started by userland/homtom-sshd. */
#define INETD_MODE 0
#define NON_INETD_MODE 1

/* Public-key login only. Password and PAM code is not compiled in, so -s and -g are no-ops kept as a second guard. */
#define DROPBEAR_SVR_PASSWORD_AUTH 0
#define DROPBEAR_SVR_PAM_AUTH 0
#define DROPBEAR_SVR_PUBKEY_AUTH 1
#define DROPBEAR_SVR_PUBKEY_OPTIONS 0
#define DROPBEAR_USE_PASSWORD_ENV 0


/* No forwarding of any kind. */
#define DROPBEAR_X11FWD 0
#define DROPBEAR_SVR_AGENTFWD 0
#define DROPBEAR_SVR_LOCALTCPFWD 0
#define DROPBEAR_SVR_REMOTETCPFWD 0
#define DROPBEAR_SVR_LOCALSTREAMFWD 0
#define DROPBEAR_SVR_REMOTESTREAMFWD 0
#define DROPBEAR_CLI_AGENTFWD 0
#define DROPBEAR_CLI_LOCALTCPFWD 0
#define DROPBEAR_CLI_REMOTETCPFWD 0
#define DROPBEAR_CLI_PROXYCMD 0
#define DROPBEAR_CLI_NETCAT 0

/* Ed25519 host and user keys only; no hardware-token (sk-) keys. */
#define DROPBEAR_RSA 0
#define DROPBEAR_DSS 0
#define DROPBEAR_ECDSA 0
#define DROPBEAR_ED25519 1
#define DROPBEAR_SK_KEYS 0

/* chacha20-poly1305 only: no AES hardware on the ARM920T, and every current OpenSSH client offers it. */
#define DROPBEAR_AES128 0
#define DROPBEAR_AES256 0
#define DROPBEAR_3DES 0
#define DROPBEAR_CHACHA20POLY1305 1
#define DROPBEAR_ENABLE_CTR_MODE 0
#define DROPBEAR_ENABLE_CBC_MODE 0
#define DROPBEAR_ENABLE_GCM_MODE 0
#define DROPBEAR_SHA1_HMAC 0
#define DROPBEAR_SHA1_96_HMAC 0
#define DROPBEAR_SHA2_256_HMAC 1
#define DROPBEAR_SHA2_512_HMAC 0

/* Key exchange: mlkem768x25519 (what OpenSSH 10 prefers; without a post-quantum method it prints a warning on every
 * login) with plain curve25519 as the fallback for older clients. */
#define DROPBEAR_MLKEM768 1
#define DROPBEAR_CURVE25519 1
#define DROPBEAR_SNTRUP761 0
#define DROPBEAR_ECDH 0
#define DROPBEAR_DH_GROUP1 0
#define DROPBEAR_DH_GROUP14_SHA1 0
#define DROPBEAR_DH_GROUP14_SHA256 0
#define DROPBEAR_DH_GROUP16 0

#define DROPBEAR_SMALL_CODE 1
#define DO_MOTD 0
#define DROPBEAR_SFTPSERVER 0
#define MAX_AUTH_TRIES 3
#define MAX_UNAUTH_CLIENTS 4
#define MAX_UNAUTH_PER_IP 2

/* BusyBox puts every applet in /bin; there is no /usr or /sbin. */
#define DEFAULT_PATH "/bin"
#define DEFAULT_ROOT_PATH "/bin"
#define ED25519_PRIV_FILENAME "/etc/dropbear/dropbear_ed25519_host_key"
#define DROPBEAR_PIDFILE "/var/run/dropbear.pid"

/* No re-exec per connection: the binary is static and not position independent, so a re-exec would re-randomise only
 * the stack and heap, and it costs a fresh exec of the whole binary per login on a slow CPU. */
#define DROPBEAR_REEXEC 0
