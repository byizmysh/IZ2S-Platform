#include "gs-back-close.h"

#include <atomic>
#include <barrier>
#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    constexpr unsigned Rounds = 4096;
    std::barrier begin(2), closing(2), reaped(2);
    std::atomic<unsigned> open_acknowledgements{0};
    unsigned released_generation = 0;
    std::thread worker([&] {
        for (unsigned generation = 1; generation <= Rounds; ++generation)
        {
            begin.arrive_and_wait();
            GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::StoppingRenderer, std::memory_order_release);
            open_acknowledgements.fetch_add(1, std::memory_order_release);
            closing.arrive_and_wait();
            released_generation = generation;
            GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::Closed, std::memory_order_release);
            reaped.arrive_and_wait();
        }
    });
    for (unsigned generation = 1; generation <= Rounds; ++generation)
    {
        GSBackClose::Mtgs.store(GSBackClose::MtgsPhase::Requested, std::memory_order_release);
        assert(!GSBackClose::MtgsClosed());
        begin.arrive_and_wait();
        assert(!GSBackClose::MtgsClosed());
        closing.arrive_and_wait();
        while (!GSBackClose::MtgsClosed())
            std::this_thread::yield();
        assert(released_generation == generation);
        assert(open_acknowledgements.load(std::memory_order_acquire) == generation);
        reaped.arrive_and_wait();
    }
    worker.join();
    std::cout << "{\"repeatedCloseCycles\":" << Rounds
              << ",\"closedOnlyAfterResourceRelease\":true,\"independentOpeningAcknowledgement\":true}\n";
}
