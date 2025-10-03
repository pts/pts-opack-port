/* gcc -s -O2 -W -Wall -Wextra -ansi -pedantic -o pack pack.c */
/* gcc -fsanitize=address -g -O2 -W -Wall -Wextra -ansi -pedantic -o pack pack.c */

#define _POSIX_SOURCE 1 /* For fileno(...). */
#define _XOPEN_SOURCE  /* For S_IFMT and S_IFREG. */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/* Huffman code input to output with key
 * Output file format:
 *      PACKED flag defined below (integer)
 *      Number of chars in expanded file (float)
 *      Number of words in expanded tree (integer)
 *      Tree in 'compressed' form:
 *              If 0<=byte<=0376, expand by zero padding to left
 *              If byte=0377, next two bytes for one word
 *          Terminal nodes: First word is zero; second is character
 *          Non-terminal nodes: Incremental 0/1 pointers
 *      Code string for number of characters in expanded file
 */

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

short used, depth, freqflag ;
short tree[1024]; /* Stores tree in puttree; codes in gcode and encoding */
char code[5];
FILE * buf;
FILE * obuf;
void gcode(short, struct node *);
void sortcount(), formtree();
short compress(), puttree();

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

	for (k=1; k<argc; k++)
	{       if (argv[k][0] == '-' && argv[k][1] == '\0')
		{       freqflag = 1 - freqflag;
			continue;
		}

		sep = -1;  cp = filename;
		for (i=0; i < (LNAME-3) && (*cp = argv[k][i]); i++)
			if (*cp++ == '/') sep = i;
#ifdef	AGSM
		if (cp[-1]==SUF1 && cp[-2]==SUF0)
		{	printf ("%s: Already packed\n", filename);
			continue;
		}
#endif
		if (i >= (LNAME-3) || (i-sep) > 13)
		{       printf ("%s: File name too long\n",argv[k]);
			continue;
		}
		if ((buf = fopen(filename, "r")) == NULL)
		{       printf ("%s: Unable to open\n", argv[k]);
			continue;
		}

		fstat(fileno(buf),&status);

		if((status.st_mode & S_IFMT) != S_IFREG)
		{	printf ("%s: Not a plain file\n",filename);
			goto closein;
		}
		if( status.st_nlink != 1 )
		{	printf("'%s' has links\n", filename);
			goto closein;
		}

		*cp++ = SUF0;  *cp++ = SUF1;  *cp = '\0';
		if( stat(filename, &ostat) != -1)
		{
			printf("%s: Already exists\n", filename);
			goto closein;
		}
                  if ((obuf = fopen(filename, "w")) == NULL)
		{       printf ("%s: Unable to create\n", argv[k]);
			goto closein;
		}

		(void)!chmod(filename,status.st_mode);
		(void)!chown(filename, status.st_uid, status.st_gid);
		errno = used = 0;
		for (i = 256; i--; ) leaves[i] = 0;

		sortcount();

		if (used < 2)
		{       printf ("%s: Trivial file\n", argv[k]);
			goto forgetit;
		}

		n = &max;
		for (i = ncodes = used; i--; )
			order[i] = n = n->sortl;

		formtree();
		put_w(PACKED, obuf);
#if 0  /* Not part of the original file format in Unix System III. */
		put_w(root->freq, obuf);
#endif
		treesize = puttree();

		depth = 0;  /* Reset for reuse by gcode */
		gcode(0,root); /* leaves[i] now points to code for i */

		if (freqflag)  /* Output stats */
		{
#ifndef FLOATING_PACK
			printf ("\n%s: %ld Bytes\n",argv[k],(long)root->freq);
#else
			printf ("\n%s: %.0f Bytes\n",argv[k],root->freq);
#endif
			for (i=ncodes; i--; )
			{   n = order[i];
#ifndef FLOATING_PACK
			    printf ("%10ld%8ld%% <%3o> = <",
			       (long)n->freq, (long)(100*n->freq/root->freq),
#else
			    printf ("%10.0f%8.3f%% <%3o> = <",
			       n->freq, 100.*n->freq/root->freq,
#endif
			       n->olink.integ&0377);
			    if (n->olink.integ<040 || n->olink.integ > 0177)
			       printf (">   ");
			    else
			       printf ("%c>  ",n->olink.integ);
			    cp = (char*)leaves[n->olink.integ];
			    for (j=0; j < *cp; j++)
			       putchar('0' +
				  ((cp[1+(j>>3)] >> (7-(j&07)))&01));
			    putchar('\n');
			}
		}

		nchars = 0;
		for (i=ncodes; i--; )
			nchars += nodes[i].freq *
                            *(unsigned char*)leaves[nodes[i].olink.integ];
		nchars = (nchars + 7)/8 + treesize + 8;
#ifndef	FLOATING_PACK
		if (freqflag) printf ("%s: Packed size: %ld bytes\n",
			argv[k], (long)nchars);
#else
		if (freqflag) printf ("%s: Packed size: %.0f bytes\n",
			argv[k], nchars);
#endif
		/* If compression won't save a block, forget it */
		if ((i = (nchars+511)/512) >= (j = (root->freq+511)/512))
		{       printf ("%s: Not packed (no blocks saved)\n", argv[k]);
			goto forgetit;
		}

		fseek(buf,0,0);

		if (compress() == 0)
		{
			unlink(argv[k]);
#ifndef FLOATING_PACK
			printf ("%s: %ld%% Compression\n", argv[k],
				(long)(100*(root->freq - nchars)/root->freq));
#else
			printf ("%s: %.0f%% Compression\n", argv[k],
				100.*(root->freq - nchars)/root->freq);
#endif
		}
		else
		{
			perror( filename );
			printf ("%s: I/O Error - File unchanged\n", argv[k]);
	forgetit:       unlink(filename);
		}

		fclose (obuf);
     closein:	fclose (buf);
#if 0
		smdate( filename , status.modtime ); /* preserve modified time */
#endif
	}
	return 0;
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
	put_w(depth, b); /* Size of tree */

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
