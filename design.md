# Release-Acquire Coherence

## Motivation

The CXL interconnect protocol, in particular the CXL 3.0 standard, enables multiple nodes to access a shared memory pool in a cache-coherent manner. However due to the cost of maintaining coherence over the large shared CXL memory pool, implementations of CXL 3.0 likely will only support cache-coherence over a small region of memory, while the rest of the larger shared memory pool would have no hardware-supported cache coherence. In short, the CXL shared memory will be divided into two regions:

- CXL-CH (Cache-coherent) of up to tens of TBs
- CXL-NCH (Non-cache-coherent) of a few GBs 

To share data across nodes in the CXL-NCH region without consistency issues, any node that writes a cache line in the region would have to explicitely write back the cache line to memory (let's call it post-writeback) before other nodes read or partially write it. Note that partial writes are problematic with stale cache lines as the unwritten stale part may be written back, depending on the cache implementation. A more vexing problem is that the node reading or partially writing remotely modfied cache lines also has to invalidate its local copy beforehand (let's call it pre-invalidate), and there is no reliable way for the software to know whether some data is cached, because of mechanisms like cache prefetching and branch predictions. For CXL-NCH regions, post-writeback is unavoidable, but repeated writeback of the same cache line may be avoided. Avoiding pre-invalidates can be more subtle as they depends on each node's knowledge about remotely modified cache line. 

In the most naive approach, a node has no idea which cache line has been remotely modified. It needs to write back after each write and invalidate before read/partial write to each cache line, which significant impacts effectiveness of caching. This is the baseline that our protocol compares against. To be more precise about pre-invalidation, a node needs to 

1. Communicate to each others about modified cache lines (post-writebacks are done as part of this communication).
2. Know when a cache line can't be modified remotely.

For (1) we let nodes communicate through shared queues in the CXL-CH channels, and for (2) we leverage synchornization points and the data-race freedom assumptions.

## Communicating Modified Cache Lines

For requirement (1), we make use of the following components: 

- a node-local shared queue per node
- a node-local cache agent thread
- a node-local invalid cache line directory
- a thread-local dirty cache line set

They are used as followed: We let each node log its modified cache line, like is done in transactional memory or persistent memory systems, and publish them through in the CXL-CH regions with shared queues. Putting queues in non-coherent regions is also an option, though it forces the log readers to writeback and invalidate the logs, which we expect it to be slower than a queue of limited size in cache-coherent regions. Each node publishes logs in their dedicated queues, and consumes the other nodes' logs to learn about remotely modified cache lines. Note that a published log must remain published until it is consumed by all other connected to the memory pool. Each node run dedicated cache agent responsible for consuming logs and record remotely modified cache lines in a per-node directory, which can then be checked to see if pre-invalidates are necessary. To reduce redundant writebacks as well traffic from log writing, we accumulate the modified cache line addresses in a thread-local set data structure, and publishing the set into shared queues periodically. The writebacks is done alongside publication to avoid messing with the cache while the user program is doing work. A straightforward policy currently used is to publish once the set reaches a certain size or at synchronization points. Other policies are also possibe, like only publishing the set at synchronization points and log+writeback cache lines that overflow the set data structure, though policies that gradually write to logs puts more pressure on the memory capacity of the limited CXL-CH regions dedicated to logs.   

## Confining Remote Modifications

For requireent (2), we observe that the commonly assumed data-race-freedom model may be useful. If we put all atomic data used for synchronization into CXL-CH regions, then data-race freedom makes sure no concurrent modification to non-atomic data is possible. Since our basic unit is a cache line, we need to assume a stronger model of cache-line-race-freedom, i.e. no concurrent modificaiton to non-atomic cache line, which can be guaranteed by when there is no false sharing of cache line on top of data-race-freedom. No false-sharing then becomes an additional requirement on user programs, though having no false sharing is known to be beneficial for concurrent code in general. 

As data-race freedom is established through synchronization points such as lock acquires and releases, we treat them as synchronizations points for log publishing/consuming as well. This means each node must publish logs of all modified cache lines before a release write, whereas on acquire read of some release write, the node need to know about all remote cache line modifications that happens before the release write according to the happens-before order from the data-race free model, by having consumed the relevant log entries. The happens-before order is tracked through vector clocks like in dynamic analysesm, though more efficient representations should be possible. The clock for each atomic location is stored as metadata in the CXL-CH region, while each node maintains two vector clocks - one user clock that carries the timestamp of its next release write, and a cache clock representing the its progress on consuming logs of other nodes. The cache clock is incremented when it has read all log before a remote release write, with the logs also following the happens before order. If it is less than the clock of an acquired location, the acquiring thread may not proceed until its node has consumed all the requisite logs. 

## Runtime system actions

In summary, the runtime system does the following on user program actions

- on plain store: pre-invalidate based on CL directory, store, update dirty-CL set
- on release store: publish log, update user clock, update atomic location clock, release store 
- on plain load: pre-invalidate based on CL directory, store
- on acquire load: wait until cache clock >= atomic location clock, acquire load, update user lock.

while a per-node cache agent runs in the background.

## Optimizations

## Evaluation Results
