/*
 * pcat.c: a port of the decompressor of the old pack compressor by Steve Zucker to modern C compilers
 * proting of pcat.c by pts@fazekas.hu at Fri Oct  3 19:36:09 CEST 2025
 *
 * Compile with: gcc -s -O2 -W -Wall -Wextra -ansi -pedantic -o pcat pcat.c
 *
 * pack does Huffman compression. There are the old and the new file formats
 * and algorithms. This is the implementation of the old algorithm, by Steve
 * Zucker no later than 1977-07-13. The new one is written by Thomas G.
 * Szymanski no later than 1980-04-11.
 *
 * More details of the old pack file format:
 *
 *   PACKED flag defined below (16-bit PDP-11 word)
 *   Number of chars in expanded file (32-bit PDP-11 float (not supported here) or 32-bit PDP-11 word)
 *   Number of words in expanded tree (16-bit PDP-11 word)
 *   Tree in 'compressed' form:
 *           If 0<=byte<=0376, expand by zero padding to left
 *           If byte=0377, next two bytes for one word (16-bit PDP-11 word)
 *       Terminal nodes: First word is zero; second is character
 *       Non-terminal nodes: Incremental 0/1 pointers
 *   Code string for number of characters in expanded file
 *
 * More info about pack: https://en.wikipedia.org/wiki/Pack_(software)
 * More info about pack: http://fileformats.archiveteam.org/wiki/Pack_(Unix)
 * More info about pack: https://retrocomputing.stackexchange.com/q/32120
 *
 * Please note that this deecompressor implementation doesn't check for
 * valid input, It may do out-of-bound memory reads and writes or fall to an
 * infinite loop on invalid input.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#define PACKED 017437 /* <US><US> - Unlikely value. 0x1f 0x1f.  */

static void fatal_eof(void) {
	fprintf(stderr, "fatal: EOF in old pack stream\n");
	exit(4);
}

static int get_w(FILE *buf) {
	register int c, d;
	if ((c = getc(buf)) < 0) fatal_eof();
	if ((d = getc(buf)) < 0) fatal_eof();
	return d << 8 | c;
}

static void decompress(FILE *buf, FILE *obuf) {
	static short tree[1024];
	register short tp, bit, word;
	register short i, *t;
	short keysize, hi, lo;
	long size;
#ifdef USE_DEBUG2
	short *u;
#endif
	obuf = stdout;
	if (get_w(buf) != PACKED) {
		fprintf(stderr, "fatal: old pack signature not found\n");
		exit(6);
	}
	hi = get_w(buf);
	/* https://archive.org/download/bitsavers_decpdp11meoatingPointFormat_1047674/701110_The_PDP-11_Floating_Point_Format.pdf */
	if (hi < 0 || hi > 040000) {
		fprintf(stderr, "fatal: size as PDP-11 32-bit float not supported\n");
		exit(2);
	}
	lo = get_w(buf);
	size = (long)hi << 16 | lo;
	t = tree;
	for (keysize = get_w(buf); keysize--; )
	{
		if ((i = getc(buf)) == 0377) {
			*t++ = get_w(buf);
		} else {
			if (i < 0) fatal_eof();
			*t++ = i;
		}
	}

#ifdef USE_DEBUG2
	for (u=tree; u<t; u++ ) fprintf(stderr, "debug2: %4d: %6d  <%3o> %c\n", u-tree, *u, *u&0377, *u);
#endif

	bit = tp = 0;
	for (;;)
	{
		if (bit == 0)
		{
			word = get_w(buf);
			bit = 16;
		}
#ifdef USE_DEBUG2
		fprintf(stderr, "debug: bit: %c\n", (word & 0x8000U) ? '1' : '0');
#endif
		if (word & 0x8000U) {  /* This works no matter how large sizeof(word) is. */
			tp += tree[tp + 1];
		} else {
			tp += tree[tp];
		}
		word <<= 1;  bit--;
		if (tree[tp] == 0)
		{
			putc(tree[tp+1], obuf);
			tp = 0;
			if ((size -= 1) == 0) break;
#ifdef USE_DEBUG
			fprintf(stderr, "debug: size=%lu bit_count=%u\n", (long)size, bit);
#endif
		}
	}
	fflush(obuf);
#ifndef __MINILIBC686__
	if (ferror(buf)) {
		fprintf(stderr, "fatal: read error\n");
		exit(2);
	}
	if (ferror(obuf)) {
		fprintf(stderr, "fatal: write error\n");
		exit(3);
	}
#endif
}

int main(int argc, char **argv) {
	FILE *buf;

	(void)argc;
	if (*a++rgv == NULL) {  /* No arguments, decompress from stdin. */
		decompress(stdin, stdout);
	} else {
		for (; *argv != NULL; ++argv) {
			if ((buf = fopen(*argv, "rb")) == NULL) {
				fprintf(stderr, "fatal: error opening old packed file for reading: %s\n", *argv);
				exit(4);
			}
			decompress(buf, stdout);
			fclose(buf);
		}
	}
	return 0;
}
