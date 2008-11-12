/*
 * lib/crypto/enc_provider/aes_ctr.c
 *
 * Copyright (C) 2003, 2007, 2008 by the Massachusetts Institute of Technology.
 * All rights reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

#include "k5-int.h"
#include "enc_provider.h"
#include "aes.h"
#include "../aead.h"

#if 0
static void printd (const char *descr, krb5_data *d) {
    int i, j;
    const int r = 16;

    printf("%s:", descr);

    for (i = 0; i < d->length; i += r) {
	printf("\n  %04x: ", i);
	for (j = i; j < i + r && j < d->length; j++)
	    printf(" %02x", 0xff & d->data[j]);
#ifdef SHOW_TEXT
	for (; j < i + r; j++)
	    printf("   ");
	printf("   ");
	for (j = i; j < i + r && j < d->length; j++) {
	    int c = 0xff & d->data[j];
	    printf("%c", isprint(c) ? c : '.');
	}
#endif
    }
    printf("\n");
}
#endif

static void xorblock(unsigned char *out, const unsigned char *in)
{
    int z;
    for (z = 0; z < BLOCK_SIZE; z++)
	out[z] ^= in[z];
}

static krb5_error_code
krb5int_aes_encrypt_ctr_iov(const krb5_keyblock *key,
		            const krb5_data *ivec,
			    krb5_crypto_iov *data,
			    size_t num_data)
{
    aes_ctx ctx;
    unsigned char ctr[BLOCK_SIZE];
    size_t blockno;
    struct iov_block_state input_pos, output_pos;

    if (aes_enc_key(key->contents, key->length, &ctx) != aes_good)
	abort();

    IOV_BLOCK_STATE_INIT(&input_pos);
    IOV_BLOCK_STATE_INIT(&output_pos);

    /* Don't encrypt the header (B0) */
    input_pos.got_header = output_pos.got_header = 1;

    if (ivec != NULL)
	memcpy(ctr, ivec->data, BLOCK_SIZE);
    else
	memset(ctr, 0, BLOCK_SIZE);

    ctr[0] &= 0x7;

    blockno  = (ctr[13] << 16);
    blockno |= (ctr[14] << 8 );
    blockno |= (ctr[15]      );

    for (;;) {
	unsigned char plain[BLOCK_SIZE];
	unsigned char ectr[BLOCK_SIZE];

	krb5int_c_iov_get_block((unsigned char *)plain, BLOCK_SIZE, data, num_data, &input_pos);

	if (input_pos.iov_pos == num_data)
	    break;

	if (aes_enc_blk(ctr, ectr, &ctx) != aes_good)
	    abort();

	xorblock(plain, ectr);
	krb5int_c_iov_put_block(data, num_data, (unsigned char *)plain, BLOCK_SIZE, &output_pos);

	blockno++;

	ctr[13] = (blockno >> 16) & 0xFF;
	ctr[14] = (blockno >> 8 ) & 0xFF;
	ctr[15] = (blockno      ) & 0xFF;
    }

    if (ivec != NULL)
	memcpy(ivec->data, ctr, sizeof(ctr));

    return 0;
}

static krb5_error_code
krb5int_aes_decrypt_ctr_iov(const krb5_keyblock *key,
			    const krb5_data *ivec,
			    krb5_crypto_iov *data,
			    size_t num_data)
{
    aes_ctx ctx;
    unsigned char ctr[BLOCK_SIZE];
    size_t blockno = 0;
    struct iov_block_state input_pos, output_pos;

    if (aes_enc_key(key->contents, key->length, &ctx) != aes_good)
	abort();

    IOV_BLOCK_STATE_INIT(&input_pos);
    IOV_BLOCK_STATE_INIT(&output_pos);

    /* Don't encrypt the header (B0) */
    input_pos.got_header = output_pos.got_header = 1;

    if (ivec != NULL)
	memcpy(ctr, ivec->data, BLOCK_SIZE);
    else
	memset(ctr, 0, BLOCK_SIZE);

    ctr[0] &= 0x7;

    blockno  = (ctr[13] << 16);
    blockno |= (ctr[14] << 8 );
    blockno |= (ctr[15]      );

    for (;;) {
	unsigned char ectr[BLOCK_SIZE];
	unsigned char cipher[BLOCK_SIZE];

	krb5int_c_iov_get_block((unsigned char *)cipher, BLOCK_SIZE, data, num_data, &input_pos);

	if (input_pos.iov_pos == num_data)
	    break;

	if (aes_enc_blk(ctr, ectr, &ctx) != aes_good)
	    abort();

	xorblock(cipher, ectr);
	krb5int_c_iov_put_block(data, num_data, (unsigned char *)cipher, BLOCK_SIZE, &output_pos);

	blockno++;

	ctr[13] = (blockno >> 16) & 0xFF;
	ctr[14] = (blockno >> 8 ) & 0xFF;
	ctr[15] = (blockno      ) & 0xFF;
    }

    if (ivec != NULL)
	memcpy(ivec->data, ctr, sizeof(ctr));

    return 0;
}

krb5_error_code
krb5int_aes_encrypt_ctr(const krb5_keyblock *key, const krb5_data *ivec,
		        const krb5_data *input, krb5_data *output)
{
    krb5_crypto_iov iov[1];
    krb5_error_code ret;

    assert(output->data != NULL);

    memcpy(output->data, input->data, input->length);
    output->length = input->length;

    iov[0].flags = KRB5_CRYPTO_TYPE_DATA;
    iov[0].data = *output;

    ret = krb5int_aes_encrypt_ctr_iov(key, ivec, iov, 1);
    if (ret != 0) {
	zap(output->data, output->length);
    }

    return ret;
}

krb5_error_code
krb5int_aes_decrypt_ctr(const krb5_keyblock *key, const krb5_data *ivec,
		        const krb5_data *input, krb5_data *output)
{
    krb5_crypto_iov iov[1];
    krb5_error_code ret;

    assert(output->data != NULL);

    memcpy(output->data, input->data, input->length);
    output->length = input->length;

    iov[0].flags = KRB5_CRYPTO_TYPE_DATA;
    iov[0].data = *output;

    ret = krb5int_aes_decrypt_ctr_iov(key, ivec, iov, 1);
    if (ret != 0) {
	zap(output->data, output->length);
    }

    return ret;
}

static krb5_error_code
k5_aes_make_key(const krb5_data *randombits, krb5_keyblock *key)
{
    if (key->length != 16 && key->length != 32)
	return(KRB5_BAD_KEYSIZE);
    if (randombits->length != key->length)
	return(KRB5_CRYPTO_INTERNAL);

    key->magic = KV5M_KEYBLOCK;

    memcpy(key->contents, randombits->data, randombits->length);
    return(0);
}

static krb5_error_code
krb5int_aes_init_state (const krb5_keyblock *key, krb5_keyusage usage,
			krb5_data *state)
{
    state->length = 16;
    state->data = (void *) malloc(16);
    if (state->data == NULL)
	return ENOMEM;
    memset(state->data, 0, state->length);
    return 0;
}

const struct krb5_enc_provider krb5int_enc_aes128_ctr = {
    16,
    16, 16,
    krb5int_aes_encrypt_ctr,
    krb5int_aes_decrypt_ctr,
    k5_aes_make_key,
    krb5int_aes_init_state,
    krb5int_default_free_state,
    krb5int_aes_encrypt_ctr_iov,
    krb5int_aes_decrypt_ctr_iov
};

const struct krb5_enc_provider krb5int_enc_aes256_ctr = {
    16,
    32, 32,
    krb5int_aes_encrypt_ctr,
    krb5int_aes_decrypt_ctr,
    k5_aes_make_key,
    krb5int_aes_init_state,
    krb5int_default_free_state,
    krb5int_aes_encrypt_ctr_iov,
    krb5int_aes_decrypt_ctr_iov
};

