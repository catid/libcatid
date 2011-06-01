/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
    Unit test for text compression
*/

#include <cat/AllCodec.hpp>
#include <iostream>
#include <conio.h> // getch()
using namespace std;
using namespace cat;

#include <cat/codec/ChatText.stats>

//#define GENERATING_TABLE








/*

Quake 3 engine Huffman algorithm 0.3

ALL the code from the public GPL source code of the Quake 3 engine 1.32

some modifications by Luigi Auriemma
e-mail: aluigi@autistici.org
web:    aluigi.org

*/
/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/* This is based on the Adaptive Huffman algorithm described in Sayood's Data
 * Compression book.  The ranks are not actually stored, but implicitly defined
 * by the location of a node within a doubly-linked list */


#define Com_Memset  memset
#define Com_Memcpy  memcpy

typedef unsigned char 		byte;

typedef enum {qfalse, qtrue}	qboolean;

#define NYT HMAX					/* NYT = Not Yet Transmitted */
#define INTERNAL_NODE (HMAX+1)

typedef struct nodetype {
	struct	nodetype *left, *right, *parent; /* tree structure */
	struct	nodetype *next, *prev; /* doubly-linked list */
	struct	nodetype **head; /* highest ranked node in block */
	int		weight;
	int		symbol;
} node_t;

#define HMAX 256 /* Maximum symbol */

typedef struct {
	int			blocNode;
	int			blocPtrs;

	node_t*		tree;
	node_t*		lhead;
	node_t*		ltail;
	node_t*		loc[HMAX+1];
	node_t**	freelist;

	node_t		nodeList[768];
	node_t*		nodePtrs[768];
} huff_t;

static int			bloc = 0;

void	Huff_putBit( int bit, byte *fout, int *offset) {
	bloc = *offset;
	if ((bloc&7) == 0) {
		fout[(bloc>>3)] = 0;
	}
	fout[(bloc>>3)] |= bit << (bloc&7);
	bloc++;
	*offset = bloc;
}

int		Huff_getBit( byte *fin, int *offset) {
	int t;
	bloc = *offset;
	t = (fin[(bloc>>3)] >> (bloc&7)) & 0x1;
	bloc++;
	*offset = bloc;
	return t;
}

/* Add a bit to the output file (buffered) */
static void add_bit (char bit, byte *fout) {
	if ((bloc&7) == 0) {
		fout[(bloc>>3)] = 0;
	}
	fout[(bloc>>3)] |= bit << (bloc&7);
	bloc++;
}

/* Receive one bit from the input file (buffered) */
static int get_bit (byte *fin) {
	int t;
	t = (fin[(bloc>>3)] >> (bloc&7)) & 0x1;
	bloc++;
	return t;
}

static node_t **get_ppnode(huff_t* huff) {
	node_t **tppnode;
	if (!huff->freelist) {
		return &(huff->nodePtrs[huff->blocPtrs++]);
	} else {
		tppnode = huff->freelist;
		huff->freelist = (node_t **)*tppnode;
		return tppnode;
	}
}

static void free_ppnode(huff_t* huff, node_t **ppnode) {
	*ppnode = (node_t *)huff->freelist;
	huff->freelist = ppnode;
}

/* Swap the location of these two nodes in the tree */
static void swap (huff_t* huff, node_t *node1, node_t *node2) {
	node_t *par1, *par2;

	par1 = node1->parent;
	par2 = node2->parent;

	if (par1) {
		if (par1->left == node1) {
			par1->left = node2;
		} else {
	      par1->right = node2;
		}
	} else {
		huff->tree = node2;
	}

	if (par2) {
		if (par2->left == node2) {
			par2->left = node1;
		} else {
			par2->right = node1;
		}
	} else {
		huff->tree = node1;
	}

	node1->parent = par2;
	node2->parent = par1;
}

/* Swap these two nodes in the linked list (update ranks) */
static void swaplist(node_t *node1, node_t *node2) {
	node_t *par1;

	par1 = node1->next;
	node1->next = node2->next;
	node2->next = par1;

	par1 = node1->prev;
	node1->prev = node2->prev;
	node2->prev = par1;

	if (node1->next == node1) {
		node1->next = node2;
	}
	if (node2->next == node2) {
		node2->next = node1;
	}
	if (node1->next) {
		node1->next->prev = node1;
	}
	if (node2->next) {
		node2->next->prev = node2;
	}
	if (node1->prev) {
		node1->prev->next = node1;
	}
	if (node2->prev) {
		node2->prev->next = node2;
	}
}

/* Do the increments */
static void increment(huff_t* huff, node_t *node) {
	node_t *lnode;

	if (!node) {
		return;
	}

	if (node->next != NULL && node->next->weight == node->weight) {
	    lnode = *node->head;
		if (lnode != node->parent) {
			swap(huff, lnode, node);
		}
		swaplist(lnode, node);
	}
	if (node->prev && node->prev->weight == node->weight) {
		*node->head = node->prev;
	} else {
	    *node->head = NULL;
		free_ppnode(huff, node->head);
	}
	node->weight++;
	if (node->next && node->next->weight == node->weight) {
		node->head = node->next->head;
	} else {
		node->head = get_ppnode(huff);
		*node->head = node;
	}
	if (node->parent) {
		increment(huff, node->parent);
		if (node->prev == node->parent) {
			swaplist(node, node->parent);
			if (*node->head == node) {
				*node->head = node->parent;
			}
		}
	}
}

void Huff_addRef(huff_t* huff, byte ch) {
	node_t *tnode, *tnode2;
	if (huff->loc[ch] == NULL) { /* if this is the first transmission of this node */
		tnode = &(huff->nodeList[huff->blocNode++]);
		tnode2 = &(huff->nodeList[huff->blocNode++]);

		tnode2->symbol = INTERNAL_NODE;
		tnode2->weight = 1;
		tnode2->next = huff->lhead->next;
		if (huff->lhead->next) {
			huff->lhead->next->prev = tnode2;
			if (huff->lhead->next->weight == 1) {
				tnode2->head = huff->lhead->next->head;
			} else {
				tnode2->head = get_ppnode(huff);
				*tnode2->head = tnode2;
			}
		} else {
			tnode2->head = get_ppnode(huff);
			*tnode2->head = tnode2;
		}
		huff->lhead->next = tnode2;
		tnode2->prev = huff->lhead;

		tnode->symbol = ch;
		tnode->weight = 1;
		tnode->next = huff->lhead->next;
		if (huff->lhead->next) {
			huff->lhead->next->prev = tnode;
			if (huff->lhead->next->weight == 1) {
				tnode->head = huff->lhead->next->head;
			} else {
				/* this should never happen */
				tnode->head = get_ppnode(huff);
				*tnode->head = tnode2;
		    }
		} else {
			/* this should never happen */
			tnode->head = get_ppnode(huff);
			*tnode->head = tnode;
		}
		huff->lhead->next = tnode;
		tnode->prev = huff->lhead;
		tnode->left = tnode->right = NULL;

		if (huff->lhead->parent) {
			if (huff->lhead->parent->left == huff->lhead) { /* lhead is guaranteed to by the NYT */
				huff->lhead->parent->left = tnode2;
			} else {
				huff->lhead->parent->right = tnode2;
			}
		} else {
			huff->tree = tnode2;
		}

		tnode2->right = tnode;
		tnode2->left = huff->lhead;

		tnode2->parent = huff->lhead->parent;
		huff->lhead->parent = tnode->parent = tnode2;

		huff->loc[ch] = tnode;

		increment(huff, tnode2->parent);
	} else {
		increment(huff, huff->loc[ch]);
	}
}

/* Get a symbol */
int Huff_Receive (node_t *node, int *ch, byte *fin) {
	while (node && node->symbol == INTERNAL_NODE) {
		if (get_bit(fin)) {
			node = node->right;
		} else {
			node = node->left;
		}
	}
	if (!node) {
		return 0;
//		Com_Error(ERR_DROP, "Illegal tree!\n");
	}
	return (*ch = node->symbol);
}

/* Get a symbol */
void Huff_offsetReceive (node_t *node, int *ch, byte *fin, int *offset) {
	bloc = *offset;
	while (node && node->symbol == INTERNAL_NODE) {
		if (get_bit(fin)) {
			node = node->right;
		} else {
			node = node->left;
		}
	}
	if (!node) {
		*ch = 0;
		return;
//		Com_Error(ERR_DROP, "Illegal tree!\n");
	}
	*ch = node->symbol;
	*offset = bloc;
}

/* Send the prefix code for this node */
static void send_huff(node_t *node, node_t *child, byte *fout) {
	if (node->parent) {
		send_huff(node->parent, node, fout);
	}
	if (child) {
		if (node->right == child) {
			add_bit(1, fout);
		} else {
			add_bit(0, fout);
		}
	}
}

/* Send a symbol */
void Huff_transmit (huff_t *huff, int ch, byte *fout) {
	int i;
	if (huff->loc[ch] == NULL) {
		/* node_t hasn't been transmitted, send a NYT, then the symbol */
		Huff_transmit(huff, NYT, fout);
		for (i = 7; i >= 0; i--) {
			add_bit((char)((ch >> i) & 0x1), fout);
		}
	} else {
		send_huff(huff->loc[ch], NULL, fout);
	}
}

void Huff_offsetTransmit (huff_t *huff, int ch, byte *fout, int *offset) {
	bloc = *offset;
	send_huff(huff->loc[ch], NULL, fout);
	*offset = bloc;
}

int Huff_DecompressPacket( unsigned char *msg, int offset, int cursize, int maxsize ) {
	int			ch, cch, i, j, size;
	byte		seq[65536];
	byte*		buffer;
	huff_t		huff;

    size = cursize - offset;
    buffer = msg + offset;

	if ( size <= 0 ) {
		return(cursize);
	}

	Com_Memset(&huff, 0, sizeof(huff_t));
	// Initialize the tree & list with the NYT node
	huff.tree = huff.lhead = huff.ltail = huff.loc[NYT] = &(huff.nodeList[huff.blocNode++]);
	huff.tree->symbol = NYT;
	huff.tree->weight = 0;
	huff.lhead->next = huff.lhead->prev = NULL;
	huff.tree->parent = huff.tree->left = huff.tree->right = NULL;

	cch = buffer[0]*256 + buffer[1];
	// don't overflow with bad messages
	if ( cch > maxsize - offset ) {
		cch = maxsize - offset;
	}
	bloc = 16;

	for ( j = 0; j < cch; j++ ) {
		ch = 0;
		// don't overflow reading from the messages
		// FIXME: would it be better to have a overflow check in get_bit ?
		if ( (bloc >> 3) > size ) {
			seq[j] = 0;
			break;
		}
		Huff_Receive(huff.tree, &ch, buffer);				/* Get a character */
		if ( ch == NYT ) {								/* We got a NYT, get the symbol associated with it */
			ch = 0;
			for ( i = 0; i < 8; i++ ) {
				ch = (ch<<1) + get_bit(buffer);
			}
		}

		seq[j] = ch;									/* Write symbol */

		Huff_addRef(&huff, (byte)ch);								/* Increment node */
	}
	cursize = cch + offset;
	Com_Memcpy(buffer, seq, cch);
    return(cursize);
}

extern 	int oldsize;

int Huff_CompressPacket( unsigned char *msg, int offset, int cursize ) {
	int			i, ch, size;
	byte		seq[65536];
	byte*		buffer;
	huff_t		huff;

    size = cursize - offset;
    buffer = msg + offset;

	if (size<=0) {
		return(cursize);
	}

	Com_Memset(&huff, 0, sizeof(huff_t));
	// Add the NYT (not yet transmitted) node into the tree/list */
	huff.tree = huff.lhead = huff.loc[NYT] =  &(huff.nodeList[huff.blocNode++]);
	huff.tree->symbol = NYT;
	huff.tree->weight = 0;
	huff.lhead->next = huff.lhead->prev = NULL;
	huff.tree->parent = huff.tree->left = huff.tree->right = NULL;
	huff.loc[NYT] = huff.tree;

	seq[0] = (size>>8);
	seq[1] = size&0xff;

	bloc = 16;

	for (i=0; i<size; i++ ) {
		ch = buffer[i];
		Huff_transmit(&huff, ch, seq);						/* Transmit symbol */
		Huff_addRef(&huff, (byte)ch);								/* Do update */
	}

	bloc += 8;												// next byte

	cursize = (bloc>>3) + offset;
	Com_Memcpy(buffer, seq, (bloc>>3));
    return(cursize);
}





void RunHuffmanTests()
{
	MersenneTwister mt;

	if (!mt.Initialize())
	{
		CAT_WARN("Huffman") << "Failed initialize MT";
		return;
	}

	for (u32 ii = 0; ii < 100; ++ii)
	{
		char test[1000];

		for (u32 jj = 0; jj < 500; ++jj)
		{
			test[jj] = jj % 60;
		}

		int new_size = Huff_CompressPacket((u8*)test, 0, 500);

		CAT_WARN("TEST") << new_size;
	}

	// 5.4 (a) (b)
	{
		CAT_WARN("Huffman") << "Problem 5.4 (a) (b)";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.49);
		factory.AddSymbol(2, 0.26);
		factory.AddSymbol(3, 0.12);
		factory.AddSymbol(4, 0.04);
		factory.AddSymbol(5, 0.04);
		factory.AddSymbol(6, 0.03);
		factory.AddSymbol(7, 0.02);

		HuffmanTree *tree = factory.BuildTree(2);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	// 5.4 (c)
	{
		CAT_WARN("Huffman") << "Problem 5.4 (c)";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.49);
		factory.AddSymbol(2, 0.26);
		factory.AddSymbol(3, 0.12);
		factory.AddSymbol(4, 0.04);
		factory.AddSymbol(5, 0.04);
		factory.AddSymbol(6, 0.03);
		factory.AddSymbol(7, 0.02);

		HuffmanTree *tree = factory.BuildTree(3);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	// 5.16 (a)
	{
		CAT_WARN("Huffman") << "Problem 5.16 (a)";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.5);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.1);
		factory.AddSymbol(4, 0.05);
		factory.AddSymbol(5, 0.05);
		factory.AddSymbol(6, 0.05);

		HuffmanTree *tree = factory.BuildTree(2);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	// 5.16 (b)
	{
		CAT_WARN("Huffman") << "Problem 5.16 (b) (c)";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.5);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.1);
		factory.AddSymbol(4, 0.05);
		factory.AddSymbol(5, 0.05);
		factory.AddSymbol(6, 0.05);
		factory.AddSymbol(7, 0.00);

		HuffmanTree *tree = factory.BuildTree(4);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	// 5.16 (e)
	{
		CAT_WARN("Huffman") << "Problem 5.16 (e) binary";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.25);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.25);
		factory.AddSymbol(4, 0.25);

		HuffmanTree *tree = factory.BuildTree(2);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}
	{
		CAT_WARN("Huffman") << "Problem 5.16 (e) quaternary";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.25);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.25);
		factory.AddSymbol(4, 0.25);

		HuffmanTree *tree = factory.BuildTree(4);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	// 5.16 (f)
	{
		CAT_WARN("Huffman") << "Problem 5.16 (f) binary";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.5);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.125);
		factory.AddSymbol(4, 0.125);

		HuffmanTree *tree = factory.BuildTree(2);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}
	{
		CAT_WARN("Huffman") << "Problem 5.16 (f) quaternary";

		HuffmanTreeFactory factory;

		factory.AddSymbol(1, 0.5);
		factory.AddSymbol(2, 0.25);
		factory.AddSymbol(3, 0.125);
		factory.AddSymbol(4, 0.125);

		HuffmanTree *tree = factory.BuildTree(4);

		CAT_WARN("Huffman") << "Expected length = " << tree->ExpectedLength();

		delete tree;
	}

	/*
		Output:

		<Huffman> Problem 5.4 (a) (b)
		<HuffmanTree> 1 = 0
		<HuffmanTree> 3 = 100
		<HuffmanTree> 7 = 10100
		<HuffmanTree> 6 = 10101
		<HuffmanTree> 4 = 10110
		<HuffmanTree> 5 = 10111
		<HuffmanTree> 2 = 11
		<Huffman> Expected length = 2.02

		<Huffman> Problem 5.4 (c)
		<HuffmanTree> 5 = 0000
		<HuffmanTree> 7 = 001000
		<HuffmanTree> 6 = 001010
		<HuffmanTree> 4 = 001001
		<HuffmanTree> 3 = 0001
		<HuffmanTree> 2 = 10
		<HuffmanTree> 1 = 01
		<Huffman> Expected length = 2.68

		<Huffman> Problem 5.16 (a)
		<HuffmanTree> 1 = 0
		<HuffmanTree> 2 = 10
		<HuffmanTree> 4 = 1100
		<HuffmanTree> 6 = 1101
		<HuffmanTree> 5 = 1110
		<HuffmanTree> 3 = 1111
		<Huffman> Expected length = 2

		<Huffman> Problem 5.16 (b) (c)
		<HuffmanTree> 2 = 00
		<HuffmanTree> 4 = 1000
		<HuffmanTree> 6 = 1010
		<HuffmanTree> 5 = 1001
		<HuffmanTree> 3 = 1011
		<HuffmanTree> 1 = 01
		<Huffman> Expected length = 2.5
	*/

	return;

	// Over 10k trials,
	for (u32 ii = 0; ii < 10000; ++ii)
	{
		HuffmanTreeFactory factory;

		u8 data[10000];
		mt.Generate(data, sizeof(data));

		// Compute symbol likelihoods
		u32 symbol_likelihood[256] = { 0 };
		for (u32 jj = 0; jj < sizeof(data); ++jj)
		{
			symbol_likelihood[data[jj]]++;
		}

		for (u32 jj = 0; jj < 256; ++jj)
		{
			symbol_likelihood[jj] = symbol_likelihood[jj] * jj / 256;
		}

		// For each symbol,
		for (u32 jj = 0; jj < 256; ++jj)
		{
			u32 symbol = jj;
			ProbabilityType probability = symbol_likelihood[jj];

			factory.AddSymbol(symbol, probability);
		}

		HuffmanTree *tree = factory.BuildTree(2);

		if (!tree)
		{
			CAT_WARN("Huffman") << "Unable to build tree!";
			return;
		}

		string compressed;

		if (!tree->Encode(data, sizeof(data), compressed))
		{
			CAT_WARN("Huffman") << "Unable to encode!";
			return;
		}

		u8 decompressed[sizeof(data)];

		u32 bytes = tree->Decode(compressed, decompressed, sizeof(decompressed));

		if (bytes != sizeof(data))
		{
			CAT_WARN("Huffman") << "Unable to decode!";
			return;
		}

		if (0 != memcmp(decompressed, data, sizeof(data)))
		{
			CAT_WARN("Huffman") << "Decode corrupted!";
			return;
		}

		CAT_INFO("Huffman") << "Compression success!  Compressed size was " << compressed.length();

		delete tree;
	}
}

int main(int argc, const char **argv)
{
	CommonLayer layer;

	if (!layer.Startup("TextCompress.cfg"))
	{
		FatalStop("Unable to initialize framework!");
		return -1;
	}

#ifndef GENERATING_TABLE
    if (!TextStatsCollector::VerifyTableIntegrity(ChatText))
    {
        CAT_WARN("Text Compression Test") << "Table integrity check failed";
    }
/*    else if (argc <= 1)
    {
        WARN("blah") << "Specify a file to read";
    }*/
    else
#endif
    {
#ifdef GENERATING_TABLE
        TextStatsCollector *collector = new TextStatsCollector();
#endif

        u32 compressed = 0;
        u32 uncompressed = 0;

        int dmax = 32768;
        int cmax = dmax*16;
        char *line = new char[dmax];
        char *comp = new char[cmax];
        char *decomp = new char[cmax];

        float worst = 0;

        const char *Files[] = {
            "bib.txt",
            "book1.txt",
            "book2.txt",
            "news.txt",
            0
        };

        double bratios[1000] = {0};
        double aratios[1000] = {0};
        int total[1000] = {0};
        double wratios[1000] = {0};

        for (int ii = 0; ii < 1000; ++ii)
        {
            bratios[ii] = 1;
        }

        double dtime = 0, ctime = 0;
        u32 linect = 0;
        int findex = 0;
        int longest = 0;
        CAT_FOREVER
        {
            const char *fname = Files[findex++];
            if (!fname) break;

            //std::ifstream file(argv[ii]);
            std::ifstream file(fname);
            if (!file)
            {
                CAT_WARN("Text Compression Test") << "File error";
            }
            else
            {
                for (;;)
                {
                    file.getline(line, dmax, '\n');
                    if (file.eof()) break;
                    ++linect;

                    //WARN("Text Compression Test") << "line: " << line;

                    int chars = 0;
                    char *x = line;
                    do
                    {
                        //WARN("Text Compression Test") << "char: " << (int)*x;
#ifdef GENERATING_TABLE
                        collector->Tally(*x);
#endif
                        ++chars;
                    } while (*x++);

#ifndef GENERATING_TABLE
                    uncompressed += chars;

                    double start = Clock::usec();
                    RangeEncoder re(comp, cmax);
                    re.Text(line, ChatText);
                    re.Finish();
                    ctime += Clock::usec() - start;
                    if (re.Fail())
                    {
                        CAT_WARN("Text Compression Test") << "Compression failure!";
                        CAT_WARN("Text Compression Test") << "txt: " << chars;
                    }
                    else
                    {
                        int used = re.Used();
                        compressed += used;

                        start = Clock::usec();
                        RangeDecoder rd(comp, used);
                        int count = rd.Text(decomp, dmax, ChatText) + 1;
                        dtime += Clock::usec() - start;

                        if (rd.Remaining() > 0)
                        {
                            CAT_WARN("Text Compression Test") << "ERROR: Unread bytes remaining";
                        }

                        float ratio = used / (float)count;
                        if (worst < ratio)
                        {
                            worst = ratio;
                            CAT_WARN("worst") << "origin   : " << line;
                        }

                        if (chars > longest)
                            longest = chars;

                        aratios[chars] += ratio;
                        total[chars]++;

                        if (wratios[chars] < ratio)
                        {
                            wratios[chars] = ratio;
                        }

                        if (bratios[chars] > ratio)
                        {
                            bratios[chars] = ratio;
                        }

                        if (used > count + 1)
                        {
                            CAT_WARN("Text Compression Test") << "ERROR: More than one extra byte emitted";
                        }

                        if (count != chars || memcmp(decomp, line, chars))
                        {
                            CAT_WARN("Text Compression Test") << "Decompression failure!";
                            CAT_WARN("Text Compression Test") << "txt.size : " << chars;
                            CAT_WARN("Text Compression Test") << "comp.size: " << used;
                            CAT_WARN("Text Compression Test") << "origin   : " << line;
                            CAT_WARN("Text Compression Test") << "decomp   : " << decomp;
                            CAT_WARN("Text Compression Test") << "out.size : " << count;
                        }
                    }
#endif
                }

                file.close();
            }
        }

#ifndef GENERATING_TABLE
        cout << "-----------------Worst ratios:" << endl;
        for (int ii = 0; ii <= longest; ++ii)
        {
            cout << ii << " letters -> " << wratios[ii] << endl;
        }

        cout << endl << "-----------------Best ratios:" << endl;
        for (int ii = 0; ii <= longest; ++ii)
        {
            cout << ii << " letters -> " << bratios[ii] << endl;
        }

        double ratio_grouped[1000] = {0};
        int total_grouped[1000] = {0};
        int highest = 0;

        cout << endl << "-----------------Average ratios:" << endl;
        for (int ii = 2; ii <= longest; ++ii)
        {
            if (total[ii])
            {
                ratio_grouped[ii / 10] += aratios[ii];
                total_grouped[ii / 10] += total[ii];
                cout << ii << " letters -> " << aratios[ii]/(double)total[ii] << endl;
                highest = ii / 10;
            }
        }

        cout << endl << "-----------------Summary:" << endl;
        for (int ii = 0; ii <= highest; ++ii)
        {
            cout << "For messages from " << ii * 10 << " to " << ((ii+1) * 10 - 1) << " characters, average ratio = " << ratio_grouped[ii]/(double)total_grouped[ii] << endl;
        }

        delete []line;
        delete []comp;
        delete []decomp;

        CAT_WARN("Text Compression Test") << "Worst message compression ratio: " << worst;
        CAT_WARN("Text Compression Test") << "uncompressed = " << uncompressed;
        CAT_WARN("Text Compression Test") << "compressed   = " << compressed;
        CAT_WARN("Text Compression Test") << "Compression rate = " << uncompressed / ctime << " MB/s";
        CAT_WARN("Text Compression Test") << "Decompression rate = " << uncompressed / dtime << " MB/s";
        CAT_WARN("Text Compression Test") << "Average input length = " << uncompressed / linect;
        CAT_WARN("Text Compression Test") << "Compression ratio = " << compressed * 100.0f / uncompressed;
        CAT_WARN("Text Compression Test") << "Table bytes = " << sizeof(_ChatText);
#else
        ofstream ofile("ChatText.stats");
        if (!ofile)
        {
            CAT_WARN("Text Compression Test") << "Unable to open file";
        }
        else
        {
            CAT_WARN("Text Compression Test") << collector->GenerateMinimalStaticTable("ChatText", ofile);
        }
        delete collector;
#endif
    }

	//// Huffman tests

	RunHuffmanTests();

    CAT_INFO("Launcher") << "** Press any key to close.";

    while (!getch())
        Sleep(100);

	layer.Shutdown();

    return 0;
}
