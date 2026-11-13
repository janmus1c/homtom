#ifndef HTTP_H
#define HTTP_H

/* Splits a complete HTTP/1.x response (read until EOF). Returns 0, or -1 when malformed. */
int http_parse(char *buf, int len, int *status, char **body, int *body_len);
int http_request(char *out, int size, const char *method, const char *host, const char *path);

#endif
