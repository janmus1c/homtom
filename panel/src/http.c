#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http.h"

int http_parse(char *buf, int len, int *status, char **body, int *body_len)
{
	char *p, *end = buf + len, *hdr_end = NULL, *cl = NULL;
	long want;

	if (len < 12 || strncmp(buf, "HTTP/1.", 7) || buf[8] != ' ')
		return -1;
	*status = atoi(buf + 9);
	if (*status < 100 || *status > 599)
		return -1;
	for (p = buf; p + 1 < end; p++) {
		if (p[0] == '\n' && p[1] == '\n') {
			hdr_end = p + 2;
			break;
		}
		if (p + 3 < end && p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
			hdr_end = p + 4;
			break;
		}
		if (p[0] == '\n' && (end - p) > 16 && !strncmp(p + 1, "Content-Length:", 15))
			cl = p + 16;
	}
	if (!hdr_end)
		return -1;
	*body = hdr_end;
	*body_len = end - hdr_end;
	if (cl) {
		want = strtol(cl, NULL, 10);
		if (want < 0 || want > *body_len)
			return -1;
		*body_len = (int)want;
	}
	return 0;
}

int http_request(char *out, int size, const char *method, const char *host, const char *path)
{
	int n = snprintf(out, size, "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: homtom-panel\r\n%s\r\n",
			 method, path, host, strcmp(method, "POST") ? "" : "Content-Length: 0\r\n");
	return n > 0 && n < size ? n : -1;
}
