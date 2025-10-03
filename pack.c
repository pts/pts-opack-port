/*
 * pack.c: a port of the old pack compressor by Steve Zucker to modern C compilers
 * initial port of pack.c by Leo Broukhis
 * more porting work and porting of unpack.c by pts@fazekas.hu at Fri Oct  3 19:36:09 CEST 2025
 *
 * Compile with: gcc -s -O2 -W -Wall -Wextra -ansi -pedantic -o pack pack.c
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
 * Please note that the deecompressor implementation doesn't check for
 * valid input, It may do out-of-bound memory reads and writes or fall to an
 * infinite loop on invalid input.
 */

#define _POSIX_SOURCE 1 /* For fileno(...). */
#define _XOPEN_SOURCE  /* For S_IFMT and S_IFREG. */
#include <stdio.h>
#include <stdlib.h>  /* For exit(...) and abort(...) if needed. */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#define	SUF0	'.'
#define	SUF1	'z'

#define NNODES 512
#define LNAME  80
#define PACKED 017437 /* <US><US> - Unlikely value */

struct node
{       struct node *zlink;  /* 0 link */
    union {
	struct node *ptr;  /* 1 link */
        short integ;
    } olink;
#ifndef FLOATING_PACK
	long freq;
#else
	float freq;
#endif
	struct node *sortl;  /* Pointer to next lower frequency node */
	struct node *sorth;  /* Pointer to next higher frequency node */
} nodes[NNODES], *root, *leaves[256], sortstart, max;

short used, depth, freqflag, sizeflag, stdoutflag, decompressflag;
short tree[1024]; /* Stores tree in puttree; codes in gcode and encoding */
char code[5];
FILE * buf;
FILE * obuf;
void gcode(short, struct node *);
void sortcount(), formtree();
short compress(), puttree();

/* --- Decompression. */

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

static int decompress(void) {  /* Decompress from buf to obuf. */
	static short tree[1024];
	register short tp, bit, word;
	register short i, *t;
	short keysize, hi, lo;
	long size;
#ifdef USE_DEBUG2
	short *u;
#endif

	if (get_w(buf) != PACKED) {
		fprintf(stderr, "fatal: old pack signature not found\n");
		return 1;
	}
	hi = get_w(buf);
	if (hi < 0 || hi > 040000) {
		fprintf(stderr, "fatal: size as PDP-11 32-bit float not supported\n");
		return 2;
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
#ifndef __MINILIBC686__
	if (ferror(buf)) {
		fprintf(stderr, "fatal: read error\n");
		return 2;
	}
	if (ferror(obuf)) {
		fprintf(stderr, "fatal: write error\n");
		return 3;
	}
#endif
	return 0;
}

static void put_w(short x, FILE *outf) {
	/* (PDP-11) little-endian word. */
	putc(x, outf);
	x >>= 8;
	putc(x, outf);
}

int main(argc, argv)
short argc; char *argv[];
{
#ifndef FLOATING_PACK
	long nchars; /* Bits, then chars in output file */
#else
	double nchars; /* Bits, then chars in output file */
#endif
	register struct node *n;
	register short i;
	register char *cp;
	char filename[LNAME];
	struct node *order[256];
	short j, k, sep, treesize, ncodes;
        struct stat status, ostat;
        int exit_code = 0;
        int had_file = 0;

	for (k=1; k<argc; k++)
	{       if (argv[k][0] == '-' && argv[k][1] == '\0')  /* - flag: print statistics to stderr. */
		{       freqflag = 1 - freqflag;
			continue;
		}
		if (argv[k][0] == '-' && argv[k][1] == 's' && argv[k][2] == '\0')  /* -s flag: keep the compressed file no matter how large it is. */
		{       sizeflag = 1 - sizeflag;
			continue;
		}
		if (argv[k][0] == '-' && argv[k][1] == 'c' && argv[k][2] == '\0')  /* -c flag: write to stdout instead of a file. */
		{       stdoutflag = 1 - stdoutflag;
			continue;
		}
		if (argv[k][0] == '-' && argv[k][1] == 'd' && argv[k][2] == '\0')  /* -d flag: decompress instead of compress. */
		{       decompressflag = 1 - decompressflag;
			continue;
		}

		had_file = 1;
		if (stdoutflag) {
			if ((buf = fopen(argv[k], "r")) == NULL)
			{       fprintf(stderr, "%s: Unable to open\n", argv[k]);
				continue;
			}
			obuf = stdout;
			if (decompressflag) {
				if (decompress()) exit_code = 1;
				fclose(buf);
				continue;
			}
			goto obuf_ok;
		}
		if (decompressflag) {
			fprintf(stderr, "%s: decompress (-d) needs stdout (-c)\n", argv[k]);
			return 1;
		}
		sep = -1;  cp = filename;
		for (i=0; i < (LNAME-3) && (*cp = argv[k][i]); i++)
			if (*cp++ == '/') sep = i;
#ifdef	AGSM
		if (cp[-1]==SUF1 && cp[-2]==SUF0)
		{	fprintf(stderr, "%s: Already packed\n", filename);
			continue;
		}
#endif
		if (i >= (LNAME-3) || (i-sep) > 13)
		{       fprintf(stderr, "%s: File name too long\n",argv[k]);
			continue;
		}
		if ((buf = fopen(filename, "r")) == NULL)
		{       fprintf(stderr, "%s: Unable to open\n", argv[k]);
			continue;
		}
		fstat(fileno(buf),&status);

		if((status.st_mode & S_IFMT) != S_IFREG)
		{	fprintf(stderr, "%s: Not a plain file\n",filename);
			goto closein;
		}
		if( status.st_nlink != 1 )
		{	fprintf(stderr, "'%s' has links\n", filename);
			goto closein;
		}

		*cp++ = SUF0;  *cp++ = SUF1;  *cp = '\0';
		if( stat(filename, &ostat) != -1)
		{
			fprintf(stderr, "%s: Already exists\n", filename);
			goto closein;
		}
                  if ((obuf = fopen(filename, "w")) == NULL)
		{       fprintf(stderr, "%s: Unable to create\n", argv[k]);
			goto closein;
		}

#ifndef __MINILIBC686__
		(void)!chmod(filename, status.st_mode);
		(void)!chown(filename, status.st_uid, status.st_gid);
#endif

		obuf_ok:
		errno = used = 0;
		for (i = 256; i--; ) leaves[i] = 0;

		sortcount();

		if (used < 2)
		{       fprintf(stderr, "%s: Trivial file\n", argv[k]);
			goto forgetit;
		}

		n = &max;
		for (i = ncodes = used; i--; )
			order[i] = n = n->sortl;

		formtree();
		put_w(PACKED, obuf);
#ifdef USE_DEBUG
		fprintf(stderr, "debug: uncompressed size: root_freq=%lu\n", (unsigned long)root->freq);
#endif
		/* Unix System III expects the uncompressed size (root->freq) as a PDP-11 dword. */
		/* PDP-11 middle-endian dword: https://en.wikipedia.org/wiki/Endianness#Middle-endian */
		put_w(root->freq >> 16, obuf);
		put_w(root->freq, obuf);
		treesize = puttree();

		depth = 0;  /* Reset for reuse by gcode */
		gcode(0,root); /* leaves[i] now points to code for i */

		if (freqflag)  /* Output stats */
		{
#ifndef FLOATING_PACK
			fprintf(stderr, "\n%s: %ld Bytes\n",argv[k],(long)root->freq);
#else
			fprintf(stderr, "\n%s: %.0f Bytes\n",argv[k],root->freq);
#endif
			for (i=ncodes; i--; )
			{   n = order[i];
#ifndef FLOATING_PACK
			    fprintf(stderr, "%10ld%8ld%% <%3o> = <",
			       (long)n->freq, (long)(100*n->freq/root->freq),
#else
			    fprintf(stderr, "%10.0f%8.3f%% <%3o> = <",
			       n->freq, 100.*n->freq/root->freq,
#endif
			       n->olink.integ&0377);
			    if (n->olink.integ<040 || n->olink.integ > 0177)
			       fprintf(stderr, ">   ");
			    else
			       fprintf(stderr, "%c>  ",n->olink.integ);
			    cp = (char*)leaves[n->olink.integ];
			    for (j=0; j < *cp; j++)
			       putchar('0' +
				  ((cp[1+(j>>3)] >> (7-(j&07)))&01));
			    putc('\n', stderr);
			}
		}

		nchars = 0;
		for (i=ncodes; i--; )
			nchars += nodes[i].freq *
                            *(unsigned char*)leaves[nodes[i].olink.integ];
		nchars = (nchars + 7)/8 + treesize + 8;
#ifndef	FLOATING_PACK
		if (freqflag) fprintf(stderr, "%s: Packed size: %ld bytes\n",
			argv[k], (long)nchars);
#else
		if (freqflag) fprintf(stderr, "%s: Packed size: %.0f bytes\n",
			argv[k], nchars);
#endif
		/* If compression won't save a block, forget it */
		if (!sizeflag && (i = (nchars+511)/512) >= (j = (root->freq+511)/512))
		{       fprintf(stderr, "%s: Not packed (no blocks saved)\n", argv[k]);
			goto forgetit;
		}

		fseek(buf,0,0);

		if (compress() == 0)
		{
			if (!stdoutflag) unlink(argv[k]);
#ifndef FLOATING_PACK
			fprintf(stderr, "%s: %ld%% Compression\n", argv[k],
				(long)(100*(root->freq - nchars)/root->freq));
#else
			fprintf(stderr, "%s: %.0f%% Compression\n", argv[k],
				100.*(root->freq - nchars)/root->freq);
#endif
		}
		else
		{
			if (!stdoutflag) perror( filename );
			fprintf(stderr, "%s: I/O Error - File unchanged\n", argv[k]);
	forgetit:       if (!stdoutflag) unlink(filename);
			exit_code = 2;  /* Had error. */
		}

		if (!stdoutflag) fclose(obuf);
     closein:	fclose (buf);
#if 0
		smdate( filename , status.modtime ); /* preserve modified time */
#endif
	}
	if (decompressflag && !had_file) {
		buf = stdin;
		obuf = stdout;
		exit_code = decompress();
	}
	return exit_code;
}

void sortcount()
{       register struct node *p, *q;
	register short c;

	max.sorth = sortstart.sorth = &max;
	sortstart.sortl = max.sortl = &sortstart;
#ifndef FLOATING_PACK
	max.freq = 2000000000;
#else
	max.freq = 1.0e+30;
#endif

	while ((c = getc(buf)) >= 0)
	{       if ((p = leaves[c]) == 0)
		{       p = leaves[c] = &nodes[used++];
			p->zlink = 0;
			p->olink.integ = c;
			p->freq = 1;
			q = p->sorth = sortstart.sorth;
			p->sortl = &sortstart;
			q->sortl = sortstart.sorth = p;
		}
		else
		{       if ((p->freq += 1) > (q = p->sorth)->freq)
			{       do
				{       q = q->sorth;
				} while (q->freq < p->freq);
				/* Move node p in front of node q */
				p->sortl->sorth = p->sorth;
				p->sorth->sortl = p->sortl;
				p->sortl = q->sortl;
				p->sorth = q;
				q->sortl->sorth = p;
				q->sortl = p;
			}
		}
	}
}

void formtree()      /* Form Huffman code tree */
{       register struct node *p, *q, *r;
	p = sortstart.sorth;

	while ((q = p->sorth) != &max)
	{       /* Create a new node by combining
		 * the two lowest frequency nodes
		 */
		r = &nodes[used++];
		r->freq = p->freq + q->freq;
		r->zlink = p;
		r->olink.ptr = q;
		p = q->sorth;
		while (r->freq > p->freq)
			p = p->sorth;
		r->sortl = p->sortl;
		r->sorth = p;
		p->sortl->sorth = r;
		p->sortl = r;
		p = q->sorth;
	}
	root = p;
}

int maketree(struct node *no);

short puttree()  /* Returns tree size (bytes) */
{       register short i,j;
	short extra; /* full words in tree */
        FILE * b = obuf;
	extra = depth = 0;
	maketree(root);
#ifdef USE_DEBUG
	fprintf(stderr, "debug: depth=0x%x\n", depth);
#endif
	put_w(depth, b); /* Size of tree */
#ifdef USE_DEBUG
	if (0) exit(5);
#endif

	for (i = 0; i<depth; i++)
	{       j = tree[i];
            if (j < 0377)
			putc(j,b);
		else
		{       putc(0377,b);
			put_w(j,b);
			extra++;
		}
	}
	return (depth + extra*2);
}

int maketree(no)
struct node *no;
{       register short d;
	register struct node *n;

	n = no;
	d = depth;
	depth += 2;
	if (n->zlink == 0) /* Terminal node */
	{       tree[d] = 0;
		tree[d+1] = n->olink.integ;
	}
	else
	{       tree[d] = maketree(n->zlink) - d;
		tree[d+1] = maketree(n->olink.ptr) - d;
	}
	return(d);
}

void gcode(short len, struct node *nod)  /* Recursive routine to compute code table */
/* len is code length at time of call */
{       register struct node *n;
	register short l, bit;
	short i;
	char *t, *u;

	n = nod;
	l = len;
	if (n->zlink == 0)
	{       /* Terminal.  Copy #bits and code, set pointer in leaves */
		/* Treat tree as char array */
                leaves[n->olink.integ] = (struct node*)(t = &((char*)tree)[depth]);
		*t++ = l;
		depth++;
		u = code;
		do
		{       *t++ = *u++;
			depth++;
		} while ((l -= 8) > 0);
	}
	else
	{       bit = 0200 >> (l&07);
		code[i = l>>3] &= ~bit;
		gcode(++l, n->zlink);
		code[i] |= bit;
		gcode(l,n->olink.ptr);
	}
}

short compress() /* Here's the time consumer */
{       register short i, word, bits;
	short c;
	char *p;

	bits = word = 0;
	while ((i = getc(buf)) >= 0)
	{       p = (char*)leaves[i];
		c = *p++ & 0377;
		for (i=0; i<c; i++) /* Output c bits */
		{       word <<= 1;
			if ((p[i>>3] << (i&07)) & 0200)
				word++;  /* Logical Or 1 */
			++bits;
			if ((bits &= 017) == 0)
			{
				put_w(word, obuf);
				if ( errno )
					return( 1 );
			}
		}
	}
	if (bits) put_w(word << (16-bits), obuf);
	fflush(obuf);
	return( errno );
}
