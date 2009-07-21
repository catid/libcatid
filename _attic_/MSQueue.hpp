// 06/11/09 part of libcat-1.0

#ifndef MS_QUEUE_HPP
#define MS_QUEUE_HPP

#include <cat/threads/RegionAllocator.hpp>

#if defined(OS_WINDOWS)
# include <windows.h>
#else
# error "Not portable to your OS!"
#endif

namespace cat {

// Lock-free Michael & Scott queue (1996)
namespace MSQ
{
#if defined(COMPILER_MSVC)
#pragma pack(push)
#pragma pack(1)
#endif

	// Union for an ABA-proof pointer
	template<class T>
	class Ptr
	{
	public:
		PACKED union
		{
			PACKED struct
			{
				T *ptr;
#if defined(ARCH_64)
				u64 ver;
#else
				u32 ver;
#endif
			};
#if defined(ARCH_64)
			volatile u64 N[2];
#else
			volatile u64 N;
#endif
		};

	public:
		Ptr(); // zero everything

		bool operator==(const Ptr<T> &rhs);
	};

#if defined(COMPILER_MSVC)
#pragma pack(pop)
#endif


	// Lock-free Michael & Scott queue (1996)
	// Performs lazy deallocation of data objects on behalf of the caller,
	// freeing all remaining objects when the Queue goes out of scope.
	template<class T>
	class Queue
	{
		Ptr<T> Head, Tail;
		HANDLE hEvent;

	public:
		Queue();
		~Queue();

	public:
		void Enqueue(T *data);
		T *Dequeue();

		// Enqueue a new event to wake up a thread stuck here
		T *DequeueWait();
	};


	// Derive from MSQ::Data<T> for data passed through the queue
	template<class T>
	class Data
	{
		Ptr<T> next;
		friend class Queue<T>;
	};


	// Compare-and-Swap atomic operation
	template<class T>
	inline bool CAS(Ptr<T> &destination, const Ptr<T> &expected, const Ptr<T> &replacement)
	{
#if defined(ARCH_64)
		Ptr<T> ComparandResult;
		ComparandResult.N[0] = expected.N[0];
		ComparandResult.N[1] = expected.N[1];
		// NOTE: Requires VS.NET 2008 or newer
		return 1 == _InterlockedCompareExchange128((__int64*)destination.N, replacement.N[1],
												   replacement.N[0], (__int64*)ComparandResult.N);
#else
		return expected.N == _InterlockedCompareExchange64((LONGLONG*)&destination.N,
														   replacement.N, expected.N);
#endif
	}


	//
	//// Templated member implementation
	//

	template<class T>
	Ptr<T>::Ptr()
	{
#if defined(ARCH_64)
		N[0] = 0;
		N[1] = 0;
#else
		N = 0;
#endif
	}

	template<class T>
	bool Ptr<T>::operator==(const Ptr<T> &n)
	{
#if defined(ARCH_64)
		return N[0] == n.N[0] && N[1] == n.N[1];
#else
		return N == n.N;
#endif
	}


	template<class T>
	Queue<T>::Queue()
	{
		hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		Head.ptr = Tail.ptr = new (RegionAllocator::ii) T;
	}

	template<class T>
	Queue<T>::~Queue()
	{
		// Destroy objects that are still queued
		for (T *next, *ptr = Head.ptr; ptr; ptr = next)
		{
			next = ptr->next.ptr;
			RegionAllocator::ii->Delete(ptr);
		}

		CloseHandle(hEvent);
	}

	template<class T>
	void Queue<T>::Enqueue(T *data)
	{
		Ptr<T> node;
		node.ptr = data;

		for (;;)
		{
			Ptr<T> tail = Tail;
			Ptr<T> next = Tail.ptr->next;

			if (tail == Tail)
			{
				if (!next.ptr)
				{
					node.ver = next.ver + 1;
					if (CAS(tail.ptr->next, next, node))
					{
						node.ver = tail.ver + 1;
						CAS(Tail, tail, node);
						break;
					}
				}
				else
				{
					next.ver = tail.ver + 1;
					CAS(Tail, tail, next);
				}
			}
		}

		// Wake up the read thread(s)
		SetEvent(hEvent);
	}

	template<class T>
	T *Queue<T>::DequeueWait()
	{
		for (;;)
		{
			// Attempt to dequeue a message
			// If we won the race to service the message, then return it
			T *retval = Dequeue();
			if (retval) return retval;

			// If the sychronization wait fails (handle closed), abort with 0
			if (WaitForSingleObject(hEvent, INFINITE) != WAIT_OBJECT_0)
				return 0;
		}
	}

	template<class T>
	T *Queue<T>::Dequeue()
	{
		for (;;)
		{
			Ptr<T> head = Head;
			Ptr<T> tail = Tail;
			Ptr<T> next = head.ptr->next;

			if (head == Head)
			{
				if (head.ptr == tail.ptr)
				{
					if (!next.ptr) return 0;

					next.ver = tail.ver + 1;
					CAS(Tail, tail, next);
				}
				else
				{
					next.ver = head.ver + 1;
					if (CAS(Head, head, next))
					{
						RegionAllocator::ii->Delete(head.ptr);
						return next.ptr;
					}
				}
			}
		}
	}
}


} // namespace cat

#endif // MS_QUEUE_HPP
