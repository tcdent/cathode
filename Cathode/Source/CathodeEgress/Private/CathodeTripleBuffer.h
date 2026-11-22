#pragma once

#include "CoreMinimal.h"

#include <atomic>

namespace Cathode
{

// A single-writer / single-reader LOCK-FREE triple-buffer for the egress's per-frame worker -> game-thread handoff. A
// lock around the deep-copied frame would be equally CORRECT; this exists to remove the per-frame lock CONTENTION, not
// for correctness. Three value slots + one atomic index ([bits 1+: ready slot][bit 0: new-flag]).
//
// PUBLISH (writer/worker): fill the writer slot, then RELEASE-exchange the index. The slot writes HAPPEN-BEFORE the
// release-store. ACQUIRE (reader/game): ACQUIRE-load the index; if a fresh value, exchange-CLAIM it. The slot read
// HAPPENS-AFTER the acquire-load. The release/acquire pair establishes the SAME happens-before edge a scope lock would, so
// the reader observes a published value WHOLE (every field of T together) or not at all — a TORN read is impossible BY
// CONSTRUCTION of the fence ordering (concurrency is proven by the fence ORDERING, not a stress race). Keeps
// only the LATEST published value — the writer recycles the un-acquired slot, so intermediate values are dropped (latest-wins
// overwrite semantics). Single-writer / single-reader ONLY.
template <typename T>
class FTripleBuffer
{
public:
	// WRITER thread only. Moves Value into the writer slot, then publishes it (release).
	void Publish(T&& Value)
	{
		Slots[WriteIndex] = MoveTemp(Value);
		const uint32 Prev = Ready.exchange((WriteIndex << 1) | 1u, std::memory_order_release);
		WriteIndex = Prev >> 1; // recycle the slot the reader/previous-ready gave back (its content is stale/moved-from)
	}

	// READER thread only. If a fresh value was published since the last acquire, claims it into OutValue + returns true.
	bool Acquire(T& OutValue)
	{
		if ((Ready.load(std::memory_order_acquire) & 1u) == 0u)
		{
			return false; // no new value
		}
		const uint32 Prev = Ready.exchange(ReadIndex << 1, std::memory_order_acquire); // claim ready, give back read, clear new-flag
		ReadIndex = Prev >> 1;
		OutValue = MoveTemp(Slots[ReadIndex]);
		return true;
	}

private:
	T Slots[3];
	std::atomic<uint32> Ready{ 0u }; // init: slot 0 is "ready", no-new (nothing published yet)
	uint32 WriteIndex = 1;           // writer-only
	uint32 ReadIndex = 2;            // reader-only
};

} // namespace Cathode
