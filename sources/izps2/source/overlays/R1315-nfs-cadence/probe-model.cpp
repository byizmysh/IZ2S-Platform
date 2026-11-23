#define FMT_HEADER_ONLY
#include "nfs-frame-witness.hpp"
#include <cassert>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace GSBackQueue {
struct Channel { struct {template<typename F> void Wait(F&& f) { assert(f()); }} space; };
}
static uint64_t ticks = 1;
static uint64_t generation = 1;
static uint64_t GetCPUTicks() { return ticks; }
static uint64_t GetTickFrequency() { return 1000; }
namespace Threading {
static uint64_t GetThreadCpuTime() { return ticks * 2; }
static uint64_t GetThreadTicksPerSecond() { return GetTickFrequency(); }
}
namespace GsBackNative {
struct CpuSample { uint64_t generation, ticks; bool valid; };
static CpuSample SampleCpu() { return {generation,ticks*3,true}; }
}
#include "gs-pool-wait.inc"

uint32_t ReadWord(const std::vector<uint8_t>& elf,uint32_t offset)
{
    assert(offset+4<=elf.size());
    const uint8_t* p=elf.data()+offset;
    return uint32_t(p[0])+(uint32_t(p[1])<<8)+(uint32_t(p[2])<<16)+(uint32_t(p[3])<<24);
}

int main(int argc,char** argv)
{
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);
    assert(input);
    const std::vector<uint8_t> elf((std::istreambuf_iterator<char>(input)),{});
    const auto first=ReadWord(elf,28);
    assert(ReadWord(elf,first)==1);
    const auto offset=ReadWord(elf,first+4),start=ReadWord(elf,first+8);
    for (const auto [pc,kind] : {std::pair{0x1d7eecu,NfsFrameWitness::Point::Iteration},
                                 std::pair{0x1d3848u,NfsFrameWitness::Point::DisplayWrite}})
    {
        const uint32_t word=ReadWord(elf,pc-start+offset);
        assert(NfsFrameWitness::Identify(pc,word)==kind);
        for (unsigned bit=0;bit<32;++bit)
            assert(NfsFrameWitness::Identify(pc,word^(1u<<bit))==NfsFrameWitness::Point::None);
        assert(NfsFrameWitness::Identify(pc+4,word)==NfsFrameWitness::Point::None);
    }
    GsPoolWait::Configure(true,true);
    std::array<unsigned char,0x2000> registers{};
    registers[0x10]=9;
    registers[0x20]=7;
    for (uint32_t field=1;field<=240;++field)
    {
        ++ticks;
        if (field==151) ++generation;
        const uint32_t packets=field==20 ? 3463 : 949;
        GsPoolWait::NoteTransition(field,packets,packets*128,registers.data());
        GsPoolWait::EndField(field);
    }
    const auto report=GsPoolWait::Report();
    size_t pos=0,count=0;
    while ((pos=report.find("IZPS2_GS_TRANSITION",pos))!=report.npos) { ++count; ++pos; }
    assert(count==32);
    assert(report.find("IZPS2_GS_TRANSITION field=12 ")!=report.npos);
    assert(report.find("IZPS2_GS_TRANSITION field=20 mtvu_packets=3463 mtvu_bytes=443264 pmode=0000000000000000 smode1=0000000000000009 smode2=0000000000000007")!=report.npos);
    assert(report.find("IZPS2_GS_TRANSITION field=43 ")!=report.npos);
    assert(report.find("IZPS2_GS_TRANSITION field=44 ")==report.npos);
    assert(report.find("start=1 fields=120 cpu_hz=1000 mtgs_valid=1 mtgs_ticks=240 back_valid=1 back_ticks=360")!=report.npos);
    assert(report.find("start=121 fields=120 cpu_hz=1000 mtgs_valid=1 mtgs_ticks=240 back_valid=0 back_ticks=0")!=report.npos);
    std::cout << "{\"witnessInstructions\":2,\"singleBitMutationsRejected\":64,\"transitionRows\":32,\"transitionFirst\":12,\"transitionTrigger\":20,\"transitionLast\":43,\"cpuGenerationCrossingDropped\":true}\n";
}
