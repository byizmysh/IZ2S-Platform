#include "nfs-podium-trigger.h"
#include "ps2_neutral_replay.h"

#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc == 7);
    uint32_t files_triggered = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream trace(argv[i]);
        assert(trace);
        NfsPodium::Trigger detector;
        detector.Arm(true);
        uint32_t field, packets, accepted = 0;
        uint64_t bytes;
        while (trace >> field >> packets >> bytes)
            if (detector.Observe(field, packets, bytes))
                ++accepted;
        assert(accepted == 1 && !detector.Armed());
        ++files_triggered;
    }

    NfsPodium::Trigger detector;
    detector.Arm(true);
    for (uint32_t i = 1; i <= 20; ++i)
        assert(!detector.Observe(2*i, 3451, 5854368));
    for (uint32_t i = 41; i <= 68; ++i)
        assert(!detector.Observe(i, 3451, i % 4 ? 5854368 : 5000000));
    detector.Arm(false);
    for (uint32_t i = 69; i <= 120; ++i)
        assert(!detector.Observe(i, 3451, 5854368));

    using State = Ps2NeutralReplay::State;
    Ps2NeutralReplay replay;
    replay.Start(240, 1200);
    for (uint32_t i = 1; i <= 1441; ++i)
        replay.Observe(i, 100 + 50ull*i);
    assert(replay.state == State::Complete && replay.first == 241 && replay.last == 1441);
    assert(replay.observations == 1201 && replay.last_tick - replay.first_tick == 60000);

    replay.Start(240, 1200);
    for (uint32_t i = 1; i <= 240; ++i)
        replay.Observe(i, 100 + 50ull*i);
    replay.Observe(242, 12200);
    assert(replay.state == State::Discontinuity && replay.observations == 0);

    replay.Start(240, 1200);
    replay.Observe(1, 100);
    replay.Observe(2, 99);
    assert(replay.state == State::Discontinuity);
    replay.Start(240, 1200);
    replay.Interrupt();
    assert(replay.state == State::Interrupted);

    replay = {};
    replay.state = State::Running;
    for (uint32_t i = 1; i <= Ps2NeutralReplay::Intervals + 1; ++i)
        replay.Observe(i, 100 + 50ull*i);
    assert(replay.state == State::Complete && replay.last == Ps2NeutralReplay::Intervals + 1);
    std::cout << "{\"nativePodiumTracesTriggered\":" << files_triggered
              << ",\"warmupFields\":240,\"measuredIntervals\":1200,"
                 "\"interruptedOrBrokenReplayRejected\":true,\"stockGtaLength\":2160}\n";
}
