/*
	Copyright (c) 2011 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of LibCat nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#include <cat/codec/Huffman.hpp>
using namespace std;
using namespace cat;

void HuffmanTree::Kill(HuffmanTreeNode *node)
{
	if (!node) return;

	for (u32 ii = 0; ii < CODE_SYMBOLS; ++ii)
		Kill(node->children[ii]);

	delete node;
}

void HuffmanTree::FillEncodings(HuffmanTreeNode *node, const BitStream &encoding)
{
	if (!node) return;

	node->encoding = encoding;

	for (u32 ii = 0; ii < CODE_SYMBOLS; ++ii)
	{
		BitStream leaf(encoding);
		leaf.writeBits(ii, CODE_SYMBOL_BITS);

		FillEncodings(node->children[ii], leaf);
	}
}

HuffmanTree::HuffmanTree()
{
}

void HuffmanTree::Initialize(HuffmanTreeNode *root)
{
	_root = root;

	BitStream empty_bs;
	FillEncodings(root, empty_bs);
}

HuffmanTree::~HuffmanTree()
{
	Kill(_root);
}

bool HuffmanTree::Encode(const string &letters, BitStream &bs)
{
	for (u32 ii = 0, len = (u32)letters.length(); ii < len; ++ii)
	{
		u32 ch = (u8)letters.at(ii);

		HuffmanTreeNode *node = _encoding_map[ch];
		if (!node) return false;

		bs << node->encoding;
	}
}

bool HuffmanTree::Decode(BitStream &bs, string &letters)
{
	if (!bs.valid()) return false;

	HuffmanTreeNode *node = _root;
	ostringstream oss;

	// While there are more bits to read,
	while (bs.unread() > 0)
	{
		u32 bits = bs.readBits(CODE_SYMBOL_BITS);

		HuffmanTreeNode *next = node->children[bits];

		// If at the end of the tree,
		if (!next)
		{
			// Write out symbol
			oss << (char)node->letter;

			// Reset to root and keep reading
			node = _root;
		}
		else
		{
			// Else- Traverse the tree down towards our target
			node = next;
		}
	}

	// If there were left over bits,
	if (node == _root) return false;

	letters = oss.str();
}

CanonicalHuffmanTreeFactory::~CanonicalHuffmanTreeFactory()
{
	if (_tree)
	{
		for (HuffmanTree::Map::iterator ii = _tree->_encoding_map.begin(); ii != _tree->_encoding_map.end(); ++ii)
		{
			HuffmanTreeNode *node = ii->second;
			delete node;
		}

		delete _tree;
	}
}

bool CanonicalHuffmanTreeFactory::AddSymbol(u32 letter, ProbabilityType probability)
{
	if (_tree)
	{
		// If already added,
		if (_tree->_encoding_map.find(letter) != _tree->_encoding_map.end())
			return false;
	}
	else
	{
		_tree = new HuffmanTree;
		if (!_tree) return false;
	}

	HuffmanTreeNode *node = new HuffmanTreeNode;
	if (!node) return false;

	node->letter = letter;
	node->probability = probability;
	CAT_OBJCLR(node->children);

	_tree->_encoding_map[letter] = node;
	_heap.push(node);

	return true;
}

HuffmanTree *CanonicalHuffmanTreeFactory::BuildTree()
{
	while (_heap.size() > 1)
	{
		HuffmanTreeNode *branch = new HuffmanTreeNode;

		CAT_OBJCLR(*branch);
		ProbabilityType probability_sum = 0.;

		// For each code symbol,
		for (u32 ii = 0; ii < CODE_SYMBOLS; ++ii)
		{
			HuffmanTreeNode *leaf = _heap.top();
			_heap.pop();

			branch->children[ii] = leaf;
			probability_sum += leaf->probability;

			if (_heap.empty())
				break;
		}

		branch->probability = probability_sum;

		// Push the combined branch back on the heap
		_heap.push(branch);
	}

	HuffmanTreeNode *root = _heap.top();
	_heap.pop();

	HuffmanTree *tree = _tree;
	tree->Initialize(root);
	_tree = 0;

	return tree;
}
