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

// 06/15/09 Improved using optimistic lock-free algorithm
// 06/11/09 part of libcat-1.0

/*
    Algorithm from "An Optimistic Approach to Lock-Free FIFO Queues"
        Edya Ladan-Mozes and Nir Shavit (2004)
*/

#ifndef LOCKLESS_FIFO_HPP
#define LOCKLESS_FIFO_HPP

#include <cat/threads/RegionAllocator.hpp>
#include <cat/threads/Atomic.hpp>

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#else
# error "Not portable to your OS!"
#endif

namespace cat {


namespace FIFO {

template<class T> class Ptr;
template<class T> class Node;
template<class T> class Queue;


//// Ptr

#if defined(CAT_COMPILER_MSVC)
# pragma pack(push)
# pragma pack(1)
#endif

    // Union for an ABA-proof pointer
    template<class T>
    class Ptr
    {
    public:
        union
        {
            struct
            {
                Node<T> *ptr;
#if defined(CAT_ARCH_64)
                u64 tag;
#else
                u32 tag;
#endif
            } CAT_PACKED;
#if defined(CAT_ARCH_64)
            volatile u64 N[2];
#else
            volatile u64 N;
#endif
        } CAT_PACKED;

        Ptr(); // zero everything

        bool operator==(const Ptr<T> &rhs);
        bool operator!=(const Ptr<T> &rhs);
    };

#if defined(CAT_COMPILER_MSVC)
# pragma pack(pop)
#endif


//// Derive from Data<T> for data passed through the queue

    template<class T>
    class Node
    {
        friend class Queue<T>;

        T *value;
        Ptr<T> next, prev;
    };


//// Queue

    // Performs lazy deallocation of data objects on behalf of the caller,
    // freeing all remaining objects when the Queue goes out of scope.
    template<class T>
    class Queue
    {
        // Pointer to head and tail
        Ptr<T> Head, Tail;

        // Event to wait on if dequeuing
#if defined(CAT_OS_WINDOWS)
        HANDLE hEvent;
#endif

    public:
        Queue();
        ~Queue();

    public:
        void Enqueue(T *data);
        T *Dequeue();
        void FixList(Ptr<T> tail, Ptr<T> head);

        // Enqueue a new event to wake up a thread stuck here
        T *DequeueWait();
    };


//// Compare-and-Swap (CAS) atomic operation

    template<class T>
    inline bool CAS(Ptr<T> &destination, const Ptr<T> &expected, const Ptr<T> &replacement)
    {
        return Atomic::CAS(&destination, &expected, &replacement);
    }


//// Ptr: Templated member implementation

    template<class T>
    Ptr<T>::Ptr()
    {
#if defined(CAT_ARCH_64)
        N[0] = 0;
        N[1] = 0;
#else
        N = 0;
#endif
    }

    template<class T>
    bool Ptr<T>::operator==(const Ptr<T> &n)
    {
#if defined(CAT_ARCH_64)
        return N[0] == n.N[0] && N[1] == n.N[1];
#else
        return N == n.N;
#endif
    }

    template<class T>
    bool Ptr<T>::operator!=(const Ptr<T> &n)
    {
#if defined(CAT_ARCH_64)
        return N[0] != n.N[0] || N[1] != n.N[1];
#else
        return N != n.N;
#endif
    }


//// Queue: Templated member implementation

    template<class T>
    Queue<T>::Queue()
    {
#if defined(CAT_OS_WINDOWS)
        hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

        Node<T> *node = new (RegionAllocator::ii) Node<T>;
        node->value = 0;

        Head.ptr = Tail.ptr = node;
    }

    template<class T>
    Queue<T>::~Queue()
    {
        // Destroy objects that are still queued
        for (Node<T> *next, *ptr = Head.ptr; ptr; ptr = next)
        {
            next = ptr->next.ptr;

            if (ptr->value)
                RegionAllocator::ii->Delete(ptr->value);
            RegionAllocator::ii->Delete(ptr);
        }

#if defined(CAT_OS_WINDOWS)
        CloseHandle(hEvent);
#endif
    }

    template<class T>
    void Queue<T>::Enqueue(T *val)
    {
        Ptr<T> tail;
        Node<T> *nd = new (RegionAllocator::ii) Node<T>;
        nd->value = val;

        for (;;)
        {
            tail = Tail;
            nd->next.ptr = tail.ptr;
            nd->next.tag = tail.tag + 1;

            Ptr<T> new_tail;
            new_tail.ptr = nd;
            new_tail.tag = tail.tag + 1;

            if (CAS(Tail, tail, new_tail))
            {
                tail.ptr->prev.ptr = nd;
                tail.ptr->prev.tag = tail.tag;
                break;
            }
        }

#if defined(CAT_OS_WINDOWS)
        SetEvent(hEvent);
#endif
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

#if defined(CAT_OS_WINDOWS)
            // If the sychronization wait fails (handle closed), abort with 0
            if (WaitForSingleObject(hEvent, INFINITE) != WAIT_OBJECT_0)
                return 0;
#endif
        }
    }

    template<class T>
    T *Queue<T>::Dequeue()
    {
        Ptr<T> tail, head, firstNodePrev;
        Node<T> *nd_dummy;
        T *val;

        for (;;)
        {
            head = Head;
            tail = Tail;
            firstNodePrev = head.ptr->prev;
            val = head.ptr->value;

            if (head == Head)
            {
                if (val != 0)
                {
                    if (tail != head)
                    {
                        if (firstNodePrev.tag != head.tag)
                        {
                            FixList(tail, head);
                            continue;
                        }
                    }
                    else
                    {
                        nd_dummy = new (RegionAllocator::ii) Node<T>;
                        nd_dummy->value = 0;
                        nd_dummy->next.ptr = tail.ptr;
                        nd_dummy->next.tag = tail.tag + 1;

                        Ptr<T> new_tail;
                        new_tail.ptr = nd_dummy;
                        new_tail.tag = tail.tag + 1;

                        if (CAS(Tail, tail, new_tail))
                        {
                            head.ptr->prev.ptr = nd_dummy;
                            head.ptr->prev.tag = tail.tag;
                        }
                        else
                        {
                            RegionAllocator::ii->Delete(nd_dummy);
                        }

                        continue;
                    }

                    Ptr<T> new_head;
                    new_head.ptr = firstNodePrev.ptr;
                    new_head.tag = head.tag + 1;

                    if (CAS(Head, head, new_head))
                    {
                        RegionAllocator::ii->Delete(head.ptr);
                        return val;
                    }
                }
                else
                {
                    if (tail.ptr == head.ptr)
                        return 0;

                    if (firstNodePrev.tag != head.tag)
                    {
                        FixList(tail, head);

                        continue;
                    }

                    Ptr<T> new_head;
                    new_head.ptr = firstNodePrev.ptr;
                    new_head.tag = head.tag + 1;

                    CAS(Head, head, new_head);
                }
            }
        }
    }

    template<class T>
    void Queue<T>::FixList(Ptr<T> tail, Ptr<T> head)
    {
        Ptr<T> curNode, curNodeNext, nextNodePrev;

        curNode = tail;

        while (head == Head && curNode != head)
        {
            curNodeNext = curNode.ptr->next;

            if (curNodeNext.tag != curNode.tag)
                return;

            nextNodePrev = curNodeNext.ptr->prev;

            if (nextNodePrev.ptr != curNode.ptr || nextNodePrev.tag != curNode.tag - 1)
            {
                curNodeNext.ptr->prev.ptr = curNode.ptr;
                curNodeNext.ptr->prev.tag = curNode.tag - 1;
            }

            curNode.ptr = curNodeNext.ptr;
            curNode.tag = curNode.tag - 1;
        }
    }


} // namespace FIFO


} // namespace cat

#endif // LOCKLESS_FIFO_HPP
