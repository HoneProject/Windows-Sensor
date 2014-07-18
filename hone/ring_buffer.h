//----------------------------------------------------------------------------
// Simple lock-free ring buffer implementation
//
// Note that the ring buffer will only roll over correctly when the size is
// a power of 2.
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers
//
// Authors
//   Alexis J. Malozemoff <alexis.malozemoff@pnnl.gov>
//   Brandon J. Carpenter <brandon.carpenter@pnnl.gov>
//   Peter L. Nordquist <peter.nordquist@pnnl.gov>
//   Richard L. Griswold <richard.griswold@pnnl.gov>
//   Ruslan A. Doroshchuk <ruslan.doroshchuk@pnnl.gov>
//----------------------------------------------------------------------------

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <limits.h>
#include "common.h"

//----------------------------------------------------------------------------
struct RING_BUFFER {
	UINT32   Front;
	UINT32   Back;
	UINT32   Length;
	void   **Buffer;
};

//----------------------------------------------------------------------------
/// @brief Initializes the ring buffer
///
/// @param ring    Ring buffer to initialize
/// @param buffer  Buffer to hold pointers to blocks
/// @param size    Size of buffer in bytes
static inline void InitRingBuffer(
	__in RING_BUFFER                      *queue,
	__in __drv_in(__drv_aliasesMem) void **buffer,
	__in ULONG                             size)
{
	queue->Front  = 0;
	queue->Back   = 0;
	queue->Buffer = buffer;
	queue->Length = size / sizeof(void*);
}

//----------------------------------------------------------------------------
/// @brief Checks if ring buffer is empty
///
/// @param ring  Ring buffer to check
///
/// @returns True if ring buffer is empty; false otherwise
static inline bool IsRingBufferEmpty(__in RING_BUFFER *ring)
{
	return (ring->Front == ring->Back) ? true : false;
}

//----------------------------------------------------------------------------
/// @brief Checks if ring buffer is full
///
/// @param ring  Ring buffer to check
///
/// @returns True if ring buffer is full; false otherwise
static inline bool IsRingBufferFull(__in RING_BUFFER *ring)
{
	return (ring->Back == (ring->Front + ring->Length)) ? true : false;
}

//----------------------------------------------------------------------------
/// @brief Gets the next block from the ring buffer
///
/// @param ring  Ring buffer to get block from
///
/// @returns Pointer to dequeued block if successful; NULL if buffer is empty
static inline void* RingBufferDequeue(__in RING_BUFFER *ring)
{
	void *block = NULL;

	for (;;) {
		// Get index of next slot in the buffer and check if the buffer is empty
		const ULONG front = ring->Front;
		if (front == ring->Back) {
			return NULL;
		}

		// Check if our slot contains anything yet
		void **slot = ring->Buffer + (front % ring->Length);
		block = *slot;
		if (!block) {
			continue;
		}

		// If our slot still contains our block, write NULL into it
		void *init = InterlockedCompareExchangePointer(slot, NULL, block);
		if (init == block) {
			break;
		}
	}

	// Increment index of next slot (assumes that there is only one reader)
	InterlockedIncrement(reinterpret_cast<LONG*>(&ring->Front));
	return block;
}

//----------------------------------------------------------------------------
/// @brief Adds a block to the back of the ring buffer
///
/// @param ring   Ring buffer to add block to
/// @param block  Block to add
///
/// @returns True if successful; false if buffer is full
static inline bool RingBufferEnqueue(
	__in RING_BUFFER                     *ring,
	__in __drv_in(__drv_aliasesMem) void *block)
{
	ULONG back;

	for (;;) {
		// Get index of next slot in the buffer and check if the buffer is full
		back = ring->Back;
		if ((back - ring->Front) >= ring->Length) {
			return false;
		}

		// Increment index of next slot if no one else has already done it
		if (static_cast<ULONG>(InterlockedCompareExchange(
				reinterpret_cast<LONG*>(&ring->Back), back + 1, back)) == back) {
			break;
		}
	}

	// Store the block into our slot
	ring->Buffer[back % ring->Length] = block;
	return true;
}

#endif  // RING_BUFFER_H
