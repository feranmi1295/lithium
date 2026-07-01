#ifndef FINGERPRINT_H
#define FINGERPRINT_H

int generate_fingerprint(char *out, size_t out_len);
int load_fingerprint(char *out, size_t out_len);
int save_fingerprint(const char *fingerprint);

#endif
