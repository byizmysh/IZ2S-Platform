#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <cassert>
#include <iostream>
#include <thread>
#include <barrier>
#include <string>

namespace GSBackQueue {
enum class RecordType { Draw, Transfer, Move };
}
static uint64_t wall = 100;
static uint64_t back_cpu = 1;
static uint64_t GetTickFrequency() { return 1000; }
namespace Threading {
static uint64_t GetThreadTicksPerSecond() { return GetTickFrequency(); }
}
namespace GsBackNative {
struct CpuSample { uint64_t generation, ticks; bool valid; };
static CpuSample SampleCpu() { return {1,back_cpu,true}; }
}
#include "gs-back-cost.inc"

uint64_t GSFieldProfile::WallTicks() { return wall; }
uint64_t GSFieldProfile::CpuTicks() { return wall; }

int main()
{
    GSFieldProfile::Capture front;
    front.Start(true);
    GsBackCost::Start(true);
    front.AtVsync(0);
    std::barrier handoff(2);
    constexpr unsigned fields=260;
    std::thread consumer([&] {
        using GSBackQueue::RecordType;
        using GSFieldProfile::Phase;
        using GSFieldProfile::Scope;
        for (unsigned field=1;field<=fields;++field)
        {
            handoff.arrive_and_wait();
            {
                GsBackCost::Record draw(RecordType::Draw);
                wall += 33;
                { GsBackCost::DetailScope config(GsBackCost::Detail::DrawConfiguration); wall += 7; }
                { Scope nested(Phase::TextureTarget); wall += 20; }
                {
                    Scope nested(Phase::VulkanDraw);
                    wall += 29;
                    GsBackCost::DetailScope state(GsBackCost::Detail::VulkanState);
                    wall += 11;
                }
            }
            { GsBackCost::Record transfer(RecordType::Transfer); wall += 15; }
            { GsBackCost::Record other(RecordType::Move); wall += 10; }
            back_cpu += 125;
            assert(!GSFieldProfile::ThreadCapture);
            handoff.arrive_and_wait();
        }
    });
    for (unsigned field=1;field<=fields;++field)
    {
        handoff.arrive_and_wait();
        handoff.arrive_and_wait();
        { GSFieldProfile::Scope front_scope(GSFieldProfile::Phase::Vsync); wall += 10; }
        { GsBackCost::DetailScope ignored(GsBackCost::Detail::VulkanState); }
        front.AtVsync(field);
        const bool sampling=GSFieldProfile::Sampling.load();
        const auto* capture=GSFieldProfile::ThreadCapture;
        GsBackCost::AtVsync(field);
        assert(GSFieldProfile::Sampling.load()==sampling && GSFieldProfile::ThreadCapture==capture);
    }
    consumer.join();
    const auto report=GsBackCost::Report();
    size_t cursor=0,count=0,previous_field=0;
    while ((cursor=report.find("IZPS2_GS_BACK_FIELD field=",cursor))!=report.npos)
    {
        const auto end=report.find('\n',cursor);
        const std::string line=report.substr(cursor,end-cursor);
        const auto field=std::stoul(line.substr(line.find('=')+1));
        assert(field>previous_field);
        previous_field=field;
        for (const char* piece : {"wall=135 cpu=125 cpu_valid=1", "outside_record=10/0", "transfer=15/1",
                                 "other_record=10/1", "draw_setup_rest=33/1", "texture_target=20/1", "vk_draw=29/1",
                                 "draw_configuration=7/1", "vk_state=11/1"})
            assert(line.find(piece)!=line.npos);
        ++count;
        cursor=end;
    }
    assert(count>=6 && count<=10);
    std::cout << "{\"sampleFields\":" << count << ",\"independentFrontState\":true,\"exclusiveNestedPhaseAccounting\":true,\"separateCpuSamples\":true}\n";
}
