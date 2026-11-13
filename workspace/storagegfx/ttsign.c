/* Print the 16-byte TTBL record signature of stdin: MD5, then each 8-byte half Blowfish-encrypted
 * with the ttsystem key. Same algorithm as tools/ttimage/mkttimage.c sign(), built from its sources:
 *   cc -O2 -I tools/ttimage -o workspace/storagegfx/ttsign workspace/storagegfx/ttsign.c \
 *      tools/ttimage/md5.c tools/ttimage/blowfish.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "md5.h"
#include "blowfish.h"

static unsigned char ttkey[16] = {0xD8, 0x88, 0xd3, 0x13, 0xed, 0x83, 0xba, 0xad,
                                  0x9c, 0xf4, 0x1b, 0x50, 0xb3, 0x43, 0xfa, 0xdd};

int main(void)
{
    struct MD5Context md5;
    BLOWFISH_CTX bf;
    unsigned char buf[65536];
    uint32_t sig[4];
    size_t n;

    MD5Init(&md5);
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
        MD5Update(&md5, buf, n);
    MD5Final((unsigned char *)sig, &md5);
    Blowfish_Init(&bf, ttkey, 16);
    Blowfish_Encrypt(&bf, &sig[0], &sig[1]);
    Blowfish_Encrypt(&bf, &sig[2], &sig[3]);
    return fwrite(sig, 1, 16, stdout) == 16 ? 0 : 1;
}
