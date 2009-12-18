/*
	Copyright (c) 2009 Christopher A. Taylor.  All rights reserved.

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

#include <cat/net/SphynxTransport.hpp>
#include <cat/port/EndianNeutral.hpp>
#include <cat/math/BitMath.hpp>
using namespace cat;
using namespace sphynx;


//// Transport Layer

Transport::Transport()
{
	CAT_OBJCLR(_recv_reliable_id);
	CAT_OBJCLR(_recv_unreliable_id);
	CAT_OBJCLR(_send_reliable_id);
	CAT_OBJCLR(_send_unreliable_id);
}

Transport::~Transport()
{
}

void Transport::OnPacket(UDPEndpoint *endpoint, u8 *data, int bytes, Connection *conn, MessageLayerHandler msg_handler)
{
	// See Transport Layer note in header.

	while (bytes >= 2)
	{
		u8 d0 = data[0];

		// reliable or unreliable?
		if (d0 & 1)
		{
			// Reliable:

			int stream = (d0 >> 2) & 7;

			// data or acknowledgment?
			if (d0 & 2)
			{
				// Acknowledgment:

				int count = (d0 >> 5) + 1;

				int chunk_len = 1 + (count << 1);

				if (chunk_len <= bytes)
				{
					u16 *ids = reinterpret_cast<u16*>( data + 1 );

					for (int ii = 0; ii < count; ++ii)
					{
						u32 id = getLE(ids[ii]);

						u32 &next_id = _send_reliable_id[stream];

						u32 nack = id & 1;

						id = ReconstructCounter<16>(next_id, id >> 1);

						// nack or ack?
						if (nack)
						{
							// Negative acknowledgment:

							// TODO
						}
						else
						{
							// Acknowledgment:

							// TODO
						}
					}
				}
			}
			else
			{
				// Data:

				int len = ((u32)data[1] << 3) | (d0 >> 5);

				int chunk_len = 4 + len;

				if (chunk_len <= bytes)
				{
					u16 *ids = reinterpret_cast<u16*>( data + 1 );

					u32 id = getLE(*ids);

					u32 nack = id & 1;

					u32 &next_id = _recv_reliable_id[stream];

					id = ReconstructCounter<16>(next_id, id >> 1);

					// ordered or unordered?
					if (stream == 0)
					{
						// Unordered:

						// TODO: Check if we've seen it already

						msg_handler(conn, data + 4, len);

						// TODO: Send ack and nacks
					}
					else
					{
						// Ordered:

						// TODO: Check if we've seen it already
						// TODO: Check if it is time to process it yet

						msg_handler(conn, data + 4, len);

						// TODO: Send ack and nacks
					}

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
		}
		else
		{
			// Unreliable:

			int stream = (d0 >> 1) & 15;

			int len = ((u32)data[1] << 3) | (d0 >> 5);

			// ordered or unordered?
			if (stream == 0)
			{
				// Unordered:

				int chunk_len = 2 + len;

				if (chunk_len <= bytes)
				{
					msg_handler(conn, data + 2, len);

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
			else
			{
				// Ordered:

				int chunk_len = 5 + len;

				if (chunk_len <= bytes)
				{
					u32 id = ((u32)data[2] << 16) | ((u32)data[3] << 8) | data[4];

					u32 &next_id = _recv_unreliable_id[stream];

					// Reconstruct the message id
					id = ReconstructCounter<24>(next_id, id);

					// If the ID is in the future,
					if ((s32)(id - next_id) >= 0)
					{
						next_id = id + 1;

						msg_handler(conn, data + 5, len);
					}

					// Continue processing remaining chunks in packet
					data += chunk_len;
					bytes -= chunk_len;
					continue;
				}
			}
		}

		// If execution reaches the end of this loop for any
		// reason, stop processing and return.
		return;
	}
}

void Transport::Tick(UDPEndpoint *endpoint)
{
}
