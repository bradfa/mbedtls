/*
 *  HMAC_DRBG implementation (NIST SP 800-90)
 *
 *  Copyright (C) 2014, Brainspark B.V.
 *
 *  This file is part of PolarSSL (http://www.polarssl.org)
 *  Lead Maintainer: Paul Bakker <polarssl_maintainer at polarssl.org>
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 *  The NIST SP 800-90A DRBGs are described in the following publication.
 *  http://csrc.nist.gov/publications/nistpubs/800-90A/SP800-90A.pdf
 *  References below are based on rev. 1 (January 2012).
 */

#include "polarssl/config.h"

#if defined(POLARSSL_HMAC_DRBG_C)

#include "polarssl/hmac_drbg.h"

/*
 * HMAC_DRBG update, using optional additional data (10.1.2.2)
 */
void hmac_drbg_update( hmac_drbg_context *ctx,
                       const unsigned char *additional, size_t add_len )
{
    size_t md_len = ctx->md_ctx.md_info->size;
    unsigned char rounds = ( additional != NULL && add_len != 0 ) ? 2 : 1;
    unsigned char sep[1];

    for( sep[0] = 0; sep[0] < rounds; sep[0]++ )
    {
        /* Step 1 or 4 */
        md_hmac_starts( &ctx->md_ctx, ctx->K, md_len );
        md_hmac_update( &ctx->md_ctx, ctx->V, md_len );
        md_hmac_update( &ctx->md_ctx, sep, 1 );
        if( rounds == 2 )
            md_hmac_update( &ctx->md_ctx, additional, add_len );
        md_hmac_finish( &ctx->md_ctx, ctx->K );

        /* Step 2 or 5 */
        md_hmac_starts( &ctx->md_ctx, ctx->K, md_len );
        md_hmac_update( &ctx->md_ctx, ctx->V, md_len );
        md_hmac_finish( &ctx->md_ctx, ctx->V );
    }
}

/*
 * Simplified HMAC_DRBG initialisation (for use with deterministic ECDSA)
 */
int hmac_drbg_init_buf( hmac_drbg_context *ctx,
                        const md_info_t * md_info,
                        const unsigned char *data, size_t data_len )
{
    int ret;

    memset( ctx, 0, sizeof( hmac_drbg_context ) );

    if( ( ret = md_init_ctx( &ctx->md_ctx, md_info ) ) != 0 )
        return( ret );

    memset( ctx->V, 0x01, md_info->size );
    /* ctx->K is already 0 */

    hmac_drbg_update( ctx, data, data_len );

    return( 0 );
}

/*
 * HMAC_DRBG reseeding (10.1.2.4)
 */
int hmac_drbg_reseed( hmac_drbg_context *ctx,
                      const unsigned char *additional, size_t len )
{
    unsigned char seed[HMAC_DRBG_MAX_SEED_INPUT];
    size_t seedlen;

    if( ctx->entropy_len + len > HMAC_DRBG_MAX_SEED_INPUT )
        return( POLARSSL_ERR_HMAC_DRBG_INPUT_TOO_BIG );

    memset( seed, 0, HMAC_DRBG_MAX_SEED_INPUT );

    /* 1a. Gather entropy_len bytes of entropy for the seed */
    if( ctx->f_entropy( ctx->p_entropy, seed, ctx->entropy_len ) != 0 )
        return( POLARSSL_ERR_HMAC_DRBG_ENTROPY_SOURCE_FAILED );

    seedlen = ctx->entropy_len;

    /* 1b. Append additional data if any */
    if( additional != NULL && len != 0 )
    {
        memcpy( seed + seedlen, additional, len );
        seedlen += len;
    }

    /* 2. Update state */
    hmac_drbg_update( ctx, seed, seedlen );

    /* 3. Reset reseed_counter (TODO) */

    /* 4. Done */
    return( 0 );
}

/*
 * HMAC_DRBG initialisation
 */
int hmac_drbg_init( hmac_drbg_context *ctx,
                    const md_info_t * md_info,
                    int (*f_entropy)(void *, unsigned char *, size_t),
                    void *p_entropy,
                    const unsigned char *custom,
                    size_t len )
{
    int ret;
    size_t entropy_len;

    memset( ctx, 0, sizeof( hmac_drbg_context ) );

    if( ( ret = md_init_ctx( &ctx->md_ctx, md_info ) ) != 0 )
        return( ret );

    /* Set initial working state */
    memset( ctx->V, 0x01, md_info->size );
    /* ctx->K is already 0 */

    ctx->f_entropy = f_entropy;
    ctx->p_entropy = p_entropy;

    /*
     * See SP800-57 5.6.1 (p. 65-66) for the security strength provided by
     * each hash function, then according to SP800-90A rev1 10.1 table 2,
     * min_entropy_len (in bits) is security_strength.
     *
     * (This also matches the sizes used in the NIST test vectors.)
     */
    entropy_len = md_info->size <= 20 ? 16 : /* 160-bits hash -> 128 bits */
                  md_info->size <= 28 ? 24 : /* 224-bits hash -> 192 bits */
                                        32;  /* better (256+) -> 256 bits */

    /*
     * For initialisation, use more entropy to emulate a nonce
     * (Again, matches test vectors.)
     */
    ctx->entropy_len = entropy_len * 3 / 2;

    if( ( ret = hmac_drbg_reseed( ctx, custom, len ) ) != 0 )
        return( ret );

    ctx->entropy_len = entropy_len;

    return( 0 );
}

/*
 * Set entropy length grabbed for reseeds
 */
void hmac_drbg_set_entropy_len( hmac_drbg_context *ctx, size_t len )
{
    ctx->entropy_len = len;
}

/*
 * HMAC_DRBG random function with optional additional data (10.1.2.5)
 */
int hmac_drbg_random_with_add( void *p_rng,
                               unsigned char *output, size_t out_len,
                               const unsigned char *additional, size_t add_len )
{
    hmac_drbg_context *ctx = (hmac_drbg_context *) p_rng;
    size_t md_len = md_get_size( ctx->md_ctx.md_info );
    size_t left = out_len;
    unsigned char *out = output;

    /* 1. Check reseed counter (TODO) */

    /* 2. Use additional data if any */
    if( additional != NULL && add_len != 0 )
        hmac_drbg_update( ctx, additional, add_len );

    /* 3, 4, 5. Generate bytes */
    while( left != 0 )
    {
        size_t use_len = left > md_len ? md_len : left;

        md_hmac_starts( &ctx->md_ctx, ctx->K, md_len );
        md_hmac_update( &ctx->md_ctx, ctx->V, md_len );
        md_hmac_finish( &ctx->md_ctx, ctx->V );

        memcpy( out, ctx->V, use_len );
        out += use_len;
        left -= use_len;
    }

    /* 6. Update */
    hmac_drbg_update( ctx, additional, add_len );

    /* 7. Update reseed counter (TODO) */

    /* 8. Done */
    return( 0 );
}

/*
 * HMAC_DRBG random function
 */
int hmac_drbg_random( void *p_rng, unsigned char *output, size_t out_len )
{
    return( hmac_drbg_random_with_add( p_rng, output, out_len, NULL, 0 ) );
}

/*
 * Free an HMAC_DRBG context
 */
void hmac_drbg_free( hmac_drbg_context *ctx )
{
    if( ctx == NULL )
        return;

    md_free_ctx( &ctx->md_ctx );

    memset( ctx, 0, sizeof( hmac_drbg_context ) );
}


#if defined(POLARSSL_SELF_TEST)

#include <stdio.h>

/*
 * Checkup routine
 */
int hmac_drbg_self_test( int verbose )
{

    if( verbose != 0 )
            printf( "\n" );

    return( 0 );
}
#endif /* POLARSSL_SELF_TEST */

#endif /* POLARSSL_HMAC_DRBG_C */
