/*
 * des.h - DES for VNC authentication (des.c).
 */
#ifndef VNCD_DES_H
#define VNCD_DES_H

#include <stdint.h>

void des_encrypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);
/* What a viewer answers to the 16-byte @challenge knowing @password (its first 8 characters) */
void vnc_auth_response(const char *password, const uint8_t challenge[16], uint8_t response[16]);

#endif
