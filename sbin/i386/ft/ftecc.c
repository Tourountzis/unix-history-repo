/*
 *  Copyright (c) 1994 Steve Gerakines
 *
 *  This is freely redistributable software.  You may do anything you
 *  wish with it, so long as the above notice stays intact.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 *  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 *  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  ftecc.c - QIC-40/80 Reed-Solomon error correction
 *  05/30/94 v1.0 ++sg
 *  Did some minor optimization.  The multiply by 0xc0 was a dog so it
 *  was replaced with a table lookup.  Fixed a couple of places where
 *  bad sectors could go unnoticed.  Moved to release.
 *
 *  03/22/94 v0.4
 *  Major re-write.  It can handle everything required by QIC now.
 *
 *  09/14/93 v0.2 pl01
 *  Modified slightly to fit with my driver.  Based entirely upon David
 *  L. Brown's package.
 */
#include <sys/ftape.h>

/* Inverse matrix */
struct inv_mat {
  UCHAR log_denom;      /* Log of the denominator */
  UCHAR zs[3][3];	/* The matrix */
};


/*
 *  Powers of x, modulo 255.
 */
static const UCHAR alpha_power[] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
  0x87, 0x89, 0x95, 0xad, 0xdd, 0x3d, 0x7a, 0xf4,
  0x6f, 0xde, 0x3b, 0x76, 0xec, 0x5f, 0xbe, 0xfb,
  0x71, 0xe2, 0x43, 0x86, 0x8b, 0x91, 0xa5, 0xcd,
  0x1d, 0x3a, 0x74, 0xe8, 0x57, 0xae, 0xdb, 0x31,
  0x62, 0xc4, 0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0x67,
  0xce, 0x1b, 0x36, 0x6c, 0xd8, 0x37, 0x6e, 0xdc,
  0x3f, 0x7e, 0xfc, 0x7f, 0xfe, 0x7b, 0xf6, 0x6b,
  0xd6, 0x2b, 0x56, 0xac, 0xdf, 0x39, 0x72, 0xe4,
  0x4f, 0x9e, 0xbb, 0xf1, 0x65, 0xca, 0x13, 0x26,
  0x4c, 0x98, 0xb7, 0xe9, 0x55, 0xaa, 0xd3, 0x21,
  0x42, 0x84, 0x8f, 0x99, 0xb5, 0xed, 0x5d, 0xba,
  0xf3, 0x61, 0xc2, 0x03, 0x06, 0x0c, 0x18, 0x30,
  0x60, 0xc0, 0x07, 0x0e, 0x1c, 0x38, 0x70, 0xe0,
  0x47, 0x8e, 0x9b, 0xb1, 0xe5, 0x4d, 0x9a, 0xb3,
  0xe1, 0x45, 0x8a, 0x93, 0xa1, 0xc5, 0x0d, 0x1a,
  0x34, 0x68, 0xd0, 0x27, 0x4e, 0x9c, 0xbf, 0xf9,
  0x75, 0xea, 0x53, 0xa6, 0xcb, 0x11, 0x22, 0x44,
  0x88, 0x97, 0xa9, 0xd5, 0x2d, 0x5a, 0xb4, 0xef,
  0x59, 0xb2, 0xe3, 0x41, 0x82, 0x83, 0x81, 0x85,
  0x8d, 0x9d, 0xbd, 0xfd, 0x7d, 0xfa, 0x73, 0xe6,
  0x4b, 0x96, 0xab, 0xd1, 0x25, 0x4a, 0x94, 0xaf,
  0xd9, 0x35, 0x6a, 0xd4, 0x2f, 0x5e, 0xbc, 0xff,
  0x79, 0xf2, 0x63, 0xc6, 0x0b, 0x16, 0x2c, 0x58,
  0xb0, 0xe7, 0x49, 0x92, 0xa3, 0xc1, 0x05, 0x0a,
  0x14, 0x28, 0x50, 0xa0, 0xc7, 0x09, 0x12, 0x24,
  0x48, 0x90, 0xa7, 0xc9, 0x15, 0x2a, 0x54, 0xa8,
  0xd7, 0x29, 0x52, 0xa4, 0xcf, 0x19, 0x32, 0x64,
  0xc8, 0x17, 0x2e, 0x5c, 0xb8, 0xf7, 0x69, 0xd2,
  0x23, 0x46, 0x8c, 0x9f, 0xb9, 0xf5, 0x6d, 0xda,
  0x33, 0x66, 0xcc, 0x1f, 0x3e, 0x7c, 0xf8, 0x77,
  0xee, 0x5b, 0xb6, 0xeb, 0x51, 0xa2, 0xc3, 0x01
};


/*
 *  Log table, modulo 255 + 1.
 */
static const UCHAR alpha_log[] = {
  0xff, 0x00, 0x01, 0x63, 0x02, 0xc6, 0x64, 0x6a,
  0x03, 0xcd, 0xc7, 0xbc, 0x65, 0x7e, 0x6b, 0x2a,
  0x04, 0x8d, 0xce, 0x4e, 0xc8, 0xd4, 0xbd, 0xe1,
  0x66, 0xdd, 0x7f, 0x31, 0x6c, 0x20, 0x2b, 0xf3,
  0x05, 0x57, 0x8e, 0xe8, 0xcf, 0xac, 0x4f, 0x83,
  0xc9, 0xd9, 0xd5, 0x41, 0xbe, 0x94, 0xe2, 0xb4,
  0x67, 0x27, 0xde, 0xf0, 0x80, 0xb1, 0x32, 0x35,
  0x6d, 0x45, 0x21, 0x12, 0x2c, 0x0d, 0xf4, 0x38,
  0x06, 0x9b, 0x58, 0x1a, 0x8f, 0x79, 0xe9, 0x70,
  0xd0, 0xc2, 0xad, 0xa8, 0x50, 0x75, 0x84, 0x48,
  0xca, 0xfc, 0xda, 0x8a, 0xd6, 0x54, 0x42, 0x24,
  0xbf, 0x98, 0x95, 0xf9, 0xe3, 0x5e, 0xb5, 0x15,
  0x68, 0x61, 0x28, 0xba, 0xdf, 0x4c, 0xf1, 0x2f,
  0x81, 0xe6, 0xb2, 0x3f, 0x33, 0xee, 0x36, 0x10,
  0x6e, 0x18, 0x46, 0xa6, 0x22, 0x88, 0x13, 0xf7,
  0x2d, 0xb8, 0x0e, 0x3d, 0xf5, 0xa4, 0x39, 0x3b,
  0x07, 0x9e, 0x9c, 0x9d, 0x59, 0x9f, 0x1b, 0x08,
  0x90, 0x09, 0x7a, 0x1c, 0xea, 0xa0, 0x71, 0x5a,
  0xd1, 0x1d, 0xc3, 0x7b, 0xae, 0x0a, 0xa9, 0x91,
  0x51, 0x5b, 0x76, 0x72, 0x85, 0xa1, 0x49, 0xeb,
  0xcb, 0x7c, 0xfd, 0xc4, 0xdb, 0x1e, 0x8b, 0xd2,
  0xd7, 0x92, 0x55, 0xaa, 0x43, 0x0b, 0x25, 0xaf,
  0xc0, 0x73, 0x99, 0x77, 0x96, 0x5c, 0xfa, 0x52,
  0xe4, 0xec, 0x5f, 0x4a, 0xb6, 0xa2, 0x16, 0x86,
  0x69, 0xc5, 0x62, 0xfe, 0x29, 0x7d, 0xbb, 0xcc,
  0xe0, 0xd3, 0x4d, 0x8c, 0xf2, 0x1f, 0x30, 0xdc,
  0x82, 0xab, 0xe7, 0x56, 0xb3, 0x93, 0x40, 0xd8,
  0x34, 0xb0, 0xef, 0x26, 0x37, 0x0c, 0x11, 0x44,
  0x6f, 0x78, 0x19, 0x9a, 0x47, 0x74, 0xa7, 0xc1,
  0x23, 0x53, 0x89, 0xfb, 0x14, 0x5d, 0xf8, 0x97,
  0x2e, 0x4b, 0xb9, 0x60, 0x0f, 0xed, 0x3e, 0xe5,
  0xf6, 0x87, 0xa5, 0x17, 0x3a, 0xa3, 0x3c, 0xb7
};


/*
 *  Multiplication table for 0xc0.
 */
static const UCHAR mult_c0[] = {
  0x00, 0xc0, 0x07, 0xc7, 0x0e, 0xce, 0x09, 0xc9,
  0x1c, 0xdc, 0x1b, 0xdb, 0x12, 0xd2, 0x15, 0xd5,
  0x38, 0xf8, 0x3f, 0xff, 0x36, 0xf6, 0x31, 0xf1,
  0x24, 0xe4, 0x23, 0xe3, 0x2a, 0xea, 0x2d, 0xed,
  0x70, 0xb0, 0x77, 0xb7, 0x7e, 0xbe, 0x79, 0xb9,
  0x6c, 0xac, 0x6b, 0xab, 0x62, 0xa2, 0x65, 0xa5,
  0x48, 0x88, 0x4f, 0x8f, 0x46, 0x86, 0x41, 0x81,
  0x54, 0x94, 0x53, 0x93, 0x5a, 0x9a, 0x5d, 0x9d,
  0xe0, 0x20, 0xe7, 0x27, 0xee, 0x2e, 0xe9, 0x29,
  0xfc, 0x3c, 0xfb, 0x3b, 0xf2, 0x32, 0xf5, 0x35,
  0xd8, 0x18, 0xdf, 0x1f, 0xd6, 0x16, 0xd1, 0x11,
  0xc4, 0x04, 0xc3, 0x03, 0xca, 0x0a, 0xcd, 0x0d,
  0x90, 0x50, 0x97, 0x57, 0x9e, 0x5e, 0x99, 0x59,
  0x8c, 0x4c, 0x8b, 0x4b, 0x82, 0x42, 0x85, 0x45,
  0xa8, 0x68, 0xaf, 0x6f, 0xa6, 0x66, 0xa1, 0x61,
  0xb4, 0x74, 0xb3, 0x73, 0xba, 0x7a, 0xbd, 0x7d,
  0x47, 0x87, 0x40, 0x80, 0x49, 0x89, 0x4e, 0x8e,
  0x5b, 0x9b, 0x5c, 0x9c, 0x55, 0x95, 0x52, 0x92,
  0x7f, 0xbf, 0x78, 0xb8, 0x71, 0xb1, 0x76, 0xb6,
  0x63, 0xa3, 0x64, 0xa4, 0x6d, 0xad, 0x6a, 0xaa,
  0x37, 0xf7, 0x30, 0xf0, 0x39, 0xf9, 0x3e, 0xfe,
  0x2b, 0xeb, 0x2c, 0xec, 0x25, 0xe5, 0x22, 0xe2,
  0x0f, 0xcf, 0x08, 0xc8, 0x01, 0xc1, 0x06, 0xc6,
  0x13, 0xd3, 0x14, 0xd4, 0x1d, 0xdd, 0x1a, 0xda,
  0xa7, 0x67, 0xa0, 0x60, 0xa9, 0x69, 0xae, 0x6e,
  0xbb, 0x7b, 0xbc, 0x7c, 0xb5, 0x75, 0xb2, 0x72,
  0x9f, 0x5f, 0x98, 0x58, 0x91, 0x51, 0x96, 0x56,
  0x83, 0x43, 0x84, 0x44, 0x8d, 0x4d, 0x8a, 0x4a,
  0xd7, 0x17, 0xd0, 0x10, 0xd9, 0x19, 0xde, 0x1e,
  0xcb, 0x0b, 0xcc, 0x0c, 0xc5, 0x05, 0xc2, 0x02,
  0xef, 0x2f, 0xe8, 0x28, 0xe1, 0x21, 0xe6, 0x26,
  0xf3, 0x33, 0xf4, 0x34, 0xfd, 0x3d, 0xfa, 0x3a
};


/*
 *  Return number of sectors available in a segment.
 */
int
sect_count(ULONG badmap)
{
  int i, amt;

  for (amt = QCV_BLKSEG, i = 0; i < QCV_BLKSEG; i++)
	if (badmap & (1 << i)) amt--;
  return(amt);
}


/*
 *  Return number of bytes available in a segment.
 */
int
sect_bytes(ULONG badmap)
{
  int i, amt;

  for (amt = QCV_SEGSIZE, i = 0; i < QCV_BLKSEG; i++)
	if (badmap & (1 << i)) amt -= QCV_BLKSIZE;
  return(amt);
}


/*
 *  Multiply two numbers in the field.
 */
static inline UCHAR
multiply(UCHAR a, UCHAR b)
{
  int tmp;

  if (!a || !b) return(0);
  tmp = alpha_log[a] + alpha_log[b];
  if (tmp > 254) tmp -= 255;
  return(alpha_power[tmp]);
}


/*
 *  Multiply by an exponent.
 */
static inline UCHAR
multiply_out(UCHAR a, int b)
{
  int tmp;

  if (!a) return(0);
  tmp = alpha_log[a] + b;
  if (tmp > 254) tmp -= 255;
  return(alpha_power[tmp]);
}


/*
 *  Divide two numbers.
 */
static inline UCHAR
divide(UCHAR a, UCHAR b)
{
  int tmp;

  if (!a || !b) return(0);
  tmp = alpha_log[a] - alpha_log[b];
  if (tmp < 0) tmp += 255;
  return (alpha_power[tmp]);
}


/*
 *  Divide using exponent.
 */
static inline UCHAR
divide_out(UCHAR a, UCHAR b)
{
  int tmp;

  if (!a) return 0;
  tmp = alpha_log[a] - b;
  if (tmp < 0) tmp += 255;
  return (alpha_power[tmp]);
}


/*
 *  This returns the value z^{a-b}.
 */
static inline UCHAR
z_of_ab(UCHAR a, UCHAR b)
{
  int tmp = a - b;

  if (tmp < 0) tmp += 255;
  return(alpha_power[tmp]);
}


/*
 *  Calculate the inverse matrix for two or three errors.  Returns 0
 *  if there is no inverse or 1 if successful.
 */
static inline int
calculate_inverse(int nerrs, int *pblk, struct inv_mat *inv)
{
  /* First some variables to remember some of the results. */
  UCHAR z20, z10, z21, z12, z01, z02;
  UCHAR i0, i1, i2;
  UCHAR iv0, iv1, iv2;

  if (nerrs < 2) return(1);
  if (nerrs > 3) return(0);

  i0 = pblk[0]; i1 = pblk[1]; i2 = pblk[2];
  if (nerrs == 2) {
	/* 2 errs */
	z01 = alpha_power[255 - i0];
	z02 = alpha_power[255 - i1];
	inv->log_denom = (z01 ^ z02);
	if (!inv->log_denom) return(0);
	inv->log_denom = 255 - alpha_log[inv->log_denom];

	inv->zs[0][0] = multiply_out(  1, inv->log_denom);
	inv->zs[0][1] = multiply_out(z02, inv->log_denom);
	inv->zs[1][0] = multiply_out(  1, inv->log_denom);
	inv->zs[1][1] = multiply_out(z01, inv->log_denom);
  } else {
	/* 3 errs */
	z20 = z_of_ab (i2, i0);
	z10 = z_of_ab (i1, i0);
	z21 = z_of_ab (i2, i1);
	z12 = z_of_ab (i1, i2);
	z01 = z_of_ab (i0, i1);
	z02 = z_of_ab (i0, i2);
	inv->log_denom = (z20 ^ z10 ^ z21 ^ z12 ^ z01 ^ z02);
	if (!inv->log_denom) return(0);
	inv->log_denom = 255 - alpha_log[inv->log_denom];

	iv0 = alpha_power[255 - i0];
	iv1 = alpha_power[255 - i1];
	iv2 = alpha_power[255 - i2];
	i0 = alpha_power[i0];
	i1 = alpha_power[i1];
	i2 = alpha_power[i2];
	inv->zs[0][0] = multiply_out(i1 ^ i2, inv->log_denom);
	inv->zs[0][1] = multiply_out(z21 ^ z12, inv->log_denom);
	inv->zs[0][2] = multiply_out(iv1 ^ iv2, inv->log_denom);
	inv->zs[1][0] = multiply_out(i0 ^ i2, inv->log_denom);
	inv->zs[1][1] = multiply_out(z20 ^ z02, inv->log_denom);
	inv->zs[1][2] = multiply_out(iv0 ^ iv2, inv->log_denom);
	inv->zs[2][0] = multiply_out(i0 ^ i1, inv->log_denom);
	inv->zs[2][1] = multiply_out(z10 ^ z01, inv->log_denom);
	inv->zs[2][2] = multiply_out(iv0 ^ iv1, inv->log_denom);
  }
  return(1);
}


/*
 *  Determine the error magnitudes for a given matrix and syndromes.
 */
static inline void
determine(int nerrs, struct inv_mat *inv, UCHAR *ss, UCHAR *es)
{
  int i, j;

  for (i = 0; i < nerrs; i++) {
	es[i] = 0;
	for (j = 0; j < nerrs; j++)
		es[i] ^= multiply(ss[j], inv->zs[i][j]);
  }
}


/*
 *  Compute the 3 syndrome values.
 */
static inline int
compute_syndromes(UCHAR *data, int nblks, int col, UCHAR *ss)
{
  UCHAR r0, r1, r2, t1, t2;
  UCHAR *rptr;

  rptr = data + col;
  data += nblks << 10;
  r0 = r1 = r2 = 0;
  while (rptr < data) {
	t1 = *rptr ^ r0;
	t2 = mult_c0[t1];
	r0 = t2 ^ r1;
	r1 = t2 ^ r2;
	r2 = t1;
	rptr += QCV_BLKSIZE;
  }
  if (r0 || r1 || r2) {
	ss[0] = divide_out(r0 ^ divide_out(r1 ^ divide_out(r2, 1), 1), nblks);
	ss[1] = r0 ^ r1 ^ r2;
	ss[2] = multiply_out(r0 ^ multiply_out(r1 ^ multiply_out(r2, 1), 1), nblks);
	return(0);
  }
  return(1);
}


/*
 *  Calculate the parity bytes for a segment, returns 0 on success (always).
 */
int
set_parity (UCHAR *data, ULONG badmap)
{
  UCHAR r0, r1, r2, t1, t2;
  UCHAR *rptr;
  int max, row, col;

  max = sect_count(badmap) - 3;
  col = QCV_BLKSIZE;
  while (col--) {
	rptr = data;
	r0 = r1 = r2 = 0;
	row = max;
	while (row--) {
		t1 = *rptr ^ r0;
		t2 = mult_c0[t1];
		r0 = t2 ^ r1;
		r1 = t2 ^ r2;
		r2 = t1;
		rptr += QCV_BLKSIZE;
	}
	*rptr = r0; rptr += QCV_BLKSIZE;
	*rptr = r1; rptr += QCV_BLKSIZE;
	*rptr = r2;
	data++;
  }
  return(0);
}


/*
 *  Check and correct errors in a block.  Returns 0 on success,
 *  1 if failed.
 */
int
check_parity(UCHAR *data, ULONG badmap, ULONG crcmap)
{
  int crcerrs, eblk[3];
  int col;
  int i, nblks;
  UCHAR ss[3], es[3];
  int i1, i2;
  struct inv_mat inv;

  nblks = sect_count(badmap);

  /* Count the number of CRC errors and note their locations. */
  crcerrs = 0;
  if (crcmap) {
	for (i = 0; i < nblks; i++) {
		if (crcmap & (1 << i)) {
			if (crcerrs == 3) return(1);
			eblk[crcerrs++] = i;
		}
	}
  }

  /* Calculate the inverse matrix */
  if (!calculate_inverse(crcerrs, eblk, &inv)) return(1);

  /* Scan each column for problems and attempt to correct. */
  for (col = 0; col < QCV_BLKSIZE; col++) {
	if (compute_syndromes(data, nblks, col, ss)) continue;
	es[0] = es[1] = es[2] = 0;

	/* Analyze the error situation. */
	switch (crcerrs) {
	    case 0:	/* 0 errors >0 failures */
		if (!ss[0]) return(1);
		eblk[crcerrs] = alpha_log[divide(ss[1], ss[0])];
		if (eblk[crcerrs] >= nblks) return(1);
		es[0] = ss[1];
		if (++crcerrs > 3) return(1);
		break;

	    case 1:	/* 1 error (+ possible failures) */
		i1 = ss[2] ^ multiply_out(ss[1], eblk[0]);
		i2 = ss[1] ^ multiply_out(ss[0], eblk[0]);
		if (!i1 && !i2) {			/* only 1 error */
			inv.zs[0][0] = alpha_power[eblk[0]];
			inv.log_denom = 0;
		} else if (!i1 || !i2) {		/* too many errors */
			return(1);
		} else {				/* add failure */
			eblk[crcerrs] = alpha_log[divide(i1, i2)];
			if (eblk[crcerrs] >= nblks) return(1);
			if (++crcerrs > 3) return(1);
			if (!calculate_inverse(crcerrs, eblk, &inv)) return(1);
		}
		determine(crcerrs, &inv, ss, es);
		break;

	    case 2:	/* 2 errors */
	    case 3:	/* 3 errors */
		determine(crcerrs, &inv, ss, es);
		break;

	    default:
		return(1);
	}

	/* Make corrections. */
	for (i = 0; i < crcerrs; i++) {
		data[(eblk[i] << 10) | col] ^= es[i];
		ss[0] ^= divide_out(es[i], eblk[i]);
		ss[1] ^= es[i];
		ss[2] ^= multiply_out(es[i], eblk[i]);
	}
	if (ss[0] || ss[1] || ss[2]) return(1);
  }
  return(0);
}
