/* ====================================================================
 * Copyright (c) 1995-2000 Carnegie Mellon University.  All rights 
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*********************************************************************
 *
 * File: s3_feat_0.c
 * 
 * Description: 
 * 	Compute a unified feature stream (Ver 0)
 *
 *		f(t) = < s2_cep(t) s2_dcep(t, 2) s2_ddcep(t, 2) >
 *	
 *	Optionally does the following transformations on MFCC before computing the
 *	derived features above:
 *
 *		1. Cepstral mean normalization (based on current utt or prior
 *			utterances).
 *		2. Automatic gain control:
 *			- subtract utter max c[0] from all c[0]
 *			- subtract estimated utter max (based on prior utterances)
 *			  from all c[0] of current utterances.
 *		3. Silence deletion
 *			- based on c[0] histogram of current utterance
 *			- based on c[0] histogram of prior utterance
 *
 * Author: 
 * 	Eric H. Thayer (eht@cs.cmu.edu)
 *********************************************************************/

/* static char rcsid[] = "@(#)$Id$"; */

#include "v1_feat.h"

#include <s3/feat.h>

#include "cep_frame.h"
#include "dcep_frame.h"
#include "ddcep_frame.h"

#include <s3/agc.h>
#include <s3/cmn.h>
#include <s3/silcomp.h>

#include <s3/ckd_alloc.h>
#include <s3/cmd_ln.h>
#include <s3/s3.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>

#define N_FEAT		1
#define FEAT_LEN	39

static uint32 n_feat = N_FEAT;
static uint32 vecsize[1] = { FEAT_LEN };
static uint32 mfcc_len = 13;

const char *
v1_feat_doc()
{
    return "1 stream :== < 12 cep + 12 dcep + 3 pow + 12 ddcep >";
}

uint32
v1_feat_id()
{
    return FEAT_ID_V1;
}

uint32
v1_feat_n_stream()
{
    return n_feat;
}

uint32
v1_feat_blksize()
{
    return vecsize[0];
}

const uint32 *
v1_feat_vecsize()
{
    return vecsize;
}

void
v1_feat_set_in_veclen(uint32 veclen)
{
    mfcc_len = veclen;

    cep_frame_set_size(veclen-1);

    vecsize[0] = 3 * (veclen-1) + 3;

    cmn_set_veclen(veclen);
    agc_set_veclen(veclen);
    cep_frame_set_size(veclen-1);
}

vector_t **
v1_feat_compute(vector_t *mfcc,
		uint32 *inout_n_frame)
{
    vector_t mfcc_frame;
    vector_t **out;
    vector_t out_frame;
    vector_t power;
    uint32 svd_n_frame;
    uint32 n_frame;
    const char *comp_type = cmd_ln_access("-silcomp");
    uint32 i, j;
    uint32 mfcc_len;
    uint32 cep_off;
    uint32 dcep_off;
    uint32 pow_off;
    uint32 ddcep_off;

    uint32 s_d_begin;
    uint32 s_d_end;

    uint32 dd_begin;
    uint32 dd_end;
    void v1_mfcc_print(vector_t *mfcc, uint32 n_frame);
    
    mfcc_len = feat_mfcc_len();		/* # of coefficients c[0..MFCC_LEN-1] per frame */

    cep_off    = 0;				/* cep feature is first and excludes c[0] */
    dcep_off   = mfcc_len - 1;			/* dcep feature includes long and short diff */
    pow_off    = 2 * (mfcc_len - 1);		/* pow feature includes c[0], diff c[0] and ddiff c[0] */
    ddcep_off  = 2 * (mfcc_len - 1) + 3;	/* ddcep feature is (mfcc_len-1) long */
    
    n_frame = svd_n_frame = *inout_n_frame;

    n_frame = sil_compression(comp_type, mfcc, n_frame);

    /* set the begin and end frames for the short dur diff cep */
    s_d_begin = dcep_frame_short_window_size();
    s_d_end = n_frame - s_d_begin;

    /* set the begin and end frames for the 2nd diff cep */
    dd_begin = ddcep_frame_window_size() + dcep_frame_short_window_size();
    dd_end   = n_frame - dd_begin;

    cmn(&mfcc[0][0], n_frame);
    agc(&mfcc[0][0], n_frame);

    
    out = feat_alloc(n_frame);

    for (i = 0, j = 0; i < n_frame; i++, j += mfcc_len) {
	out_frame = out[i][0];
	power = out_frame + pow_off;
	mfcc_frame = mfcc[i];

	cep_frame(out_frame + cep_off, power, mfcc_frame);

	if ((i >= s_d_begin) && (i < s_d_end)) {
	    short_dcep_frame(out_frame + dcep_off, power, mfcc_frame);
	}

	if ((i >= dd_begin) && (i < dd_end)) {
	    ddcep_frame(out_frame + ddcep_off, power, mfcc_frame);
	}
    }

    /* Deal w/ short diff boundaries */
    for (i = 0; i < s_d_begin; i++) {
	memcpy(&out[i][0][dcep_off],
	       &out[s_d_begin][0][dcep_off],
	       (mfcc_len-1)*sizeof(float32));

	out[i][0][pow_off+1] = out[s_d_begin][0][pow_off+1];
    }
    for (i = s_d_end; i < n_frame; i++) {
	memcpy(&out[i][0][dcep_off],
	       &out[s_d_end-1][0][dcep_off],
	       (mfcc_len-1)*sizeof(float32));

	out[i][0][pow_off+1] = out[s_d_end-1][0][pow_off+1];
    }

    /* Deal w/ 2nd diff boundaries */
    for (i = 0; i < dd_begin; i++) {
	memcpy(&out[i][0][ddcep_off],
	       &out[dd_begin][0][ddcep_off],
	       (mfcc_len-1)*sizeof(float32));

	out[i][0][pow_off+2] = out[dd_begin][0][pow_off+2];
    }
    for (i = dd_end; i < n_frame; i++) {
	memcpy(&out[i][0][ddcep_off],
	       &out[dd_end-1][0][ddcep_off],
	       (mfcc_len-1)*sizeof(float32));

	out[i][0][pow_off+2] = out[dd_end-1][0][pow_off+2];
    }

    *inout_n_frame = n_frame;

    return out;
}

void
v1_mfcc_print(vector_t *mfcc, uint32 n_frame)
{
    uint32 i, k;
    uint32 mfcc_len;

    mfcc_len = feat_mfcc_len();

    for (i = 0; i < n_frame; i++) {
	printf("mfcc[%04u]: ", i);
	for (k = 0; k < mfcc_len; k++) {
	    printf("%6.3f ", mfcc[i][k]);
	}
	printf("\n");
    }
}


void
v1_feat_print(const char *label,
	      vector_t **f,
	      uint32 n_frames)
{
    uint32 i;
    int32 j;
    uint32 k;
    char *name[] = { "" };
    vector_t *frame;
    vector_t stream;

    for (i = 0; i < n_frames; i++) {
	frame = f[i];

	for (j = 0; j < n_feat; j++) {
	    stream = frame[j];

	    printf("%s%s[%04u]: ", label, name[j], i);
	    for (k = 0; k < vecsize[j]; k++) {
		printf("%6.3f ", stream[k]);
	    }
	    printf("\n");
	}
	printf("\n");
    }
}

/*
 * Log record.  Maintained by RCS.
 *
 * $Log$
 * Revision 1.4  2004/07/21  18:05:38  egouvea
 * Changed the license terms to make it the same as sphinx2 and sphinx3.
 * 
 * Revision 1.3  2001/04/05 20:02:30  awb
 * *** empty log message ***
 *
 * Revision 1.2  2000/09/29 22:35:13  awb
 * *** empty log message ***
 *
 * Revision 1.1  2000/09/24 21:38:31  awb
 * *** empty log message ***
 *
 * Revision 1.4  97/07/16  11:36:22  eht
 * *** empty log message ***
 * 
 * Revision 1.3  1996/03/25  15:36:31  eht
 * Changes to allow for settable input feature length
 *
 * Revision 1.2  1996/01/26  18:04:51  eht
 * *** empty log message ***
 *
 * Revision 1.1  1995/12/14  20:12:58  eht
 * Initial revision
 *
 */
