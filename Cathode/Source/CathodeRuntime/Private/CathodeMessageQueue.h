#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"

#include "CathodeRuntimeSeam.h" // FHostOutboundMessage

namespace Cathode
{

// A host -> worker inbound message: a deferred action the worker applies on its own thread. The
// concrete event/state taxonomy belongs to the DOM / Host Bridge components; at this boundary a
// message is just "do this, on the worker, in order".
struct FHostInboundMessage
{
	TFunction<void()> Apply;
};

// FHostOutboundMessage (the worker -> host "frame ready" notification) lives in Public/CathodeRuntimeSeam.h
// so the surface-load owner + CathodeEgress can produce/consume it cross-module.

// A thread-safe FIFO message queue: many producer threads enqueue, the single worker thread drains in
// order. Backed by an MPSC queue; OUR contract is the FIFO drain-to-empty that the tick relies
// on — that is what the specs assert, not the underlying queue primitive (which is vendor/engine).
template <typename T>
class TCathodeMessageQueue
{
public:
	/** Enqueue from any thread. */
	void Enqueue(T&& Item) { Queue.Enqueue(MoveTemp(Item)); }
	void Enqueue(const T& Item) { Queue.Enqueue(Item); }

	/** Drain all currently-queued items into Out in FIFO order; returns the count drained. Call on the
	 *  single consumer (worker) thread only. */
	int32 Drain(TArray<T>& Out)
	{
		int32 Count = 0;
		T Item;
		while (Queue.Dequeue(Item))
		{
			Out.Add(MoveTemp(Item));
			++Count;
		}
		return Count;
	}

	bool IsEmpty() const { return Queue.IsEmpty(); }

private:
	TQueue<T, EQueueMode::Mpsc> Queue;
};

} // namespace Cathode
