#define PS2_CAPTURE_SW_FRAME 1
/* Continuous ARMSX2 application session. Normal builds run until an explicit
 * application Stop request; PS2_SESSION_VERIFICATION adds a bounded local
 * pause/resume/checkpoint scenario. Platform calls remain in existing providers. */
#include "Common.h"
#include "Memory.h"
#include "R3000A.h"
#include "R5900.h"
#include "VMManager.h"
#include "CDVD/CDVDcommon.h"
#include "ps2/BiosTools.h"
#include "MTVU.h"
#include "MTGS.h"
#include "Host.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "common/Console.h"
#include "common/MemorySettingsInterface.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "hbxl_vm_frontend.h"
#include "hbxl_vm_av.h"
#include "hbxl_stack_pool.h"
#include "sdk_threads.h"
#include "ps2_session_control.h"
#include "ps2_session_state.h"
#include "ps2_user_storage.h"
#ifdef PS2_NATIVE_SD_SAVES
#include "ps2_sd_saves.h"
#ifdef PS2_NATIVE_SD_LIVE_CARD
#include "ps2_sd_card.h"
#endif
extern "C" bool ps2_checked_memcard_flush();
#endif
#include "ps2_session_frontend.h"
#include "ps2_app_controller.h"
#include "ps2_app_menu.h"
#include "native_pad_frontend.h"
#include "sw_scanout.h"
#include <thread>
#include <mutex>
#include <atomic>
#include "StubHostTasks.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#define CHECK(x) do{if(!(x))ps2_hbxl_vm_fatal(#x,__LINE__);}while(0)
extern s32 recEeExecuteBlock(s32,u32);
extern void ps2_check_image_codec_abi();
// A separate launcher HID observer may set this one-way latch even when the
// VM has stopped presenting frames. It never kills a thread or tears down GPU
// resources; only this CPU owner's safe points consume the request.
extern "C" { alignas(4) uint32_t ps2_exit_requested=0; }
static bool quick_exit_requested(){return __atomic_load_n(&ps2_exit_requested,__ATOMIC_ACQUIRE)!=0;}
static void record_quick_exit(bool during_boot){
    FILE* f=std::fopen("ps2-exit.json","wb");CHECK(f);
    std::fprintf(f,"{\"requested\":true,\"duringBoot\":%s,\"autosaveAttempted\":false,\"cooperativeShutdown\":true}\n",during_boot?"true":"false");
    CHECK(!std::fclose(f));
}

static void record_startup(VMBootResult result,bool rollback_completed,bool cpu_initialized=true){
    FILE* f=std::fopen("hbxl-vm-startup.json","wb");CHECK(f);
    std::fprintf(f,"{\"version\":2,\"startupSucceeded\":%s,\"bootResult\":%u,\"rollbackCompleted\":%s,\"phase\":\"%s\"}\n",
        result==VMBootResult::StartupSuccess?"true":"false",static_cast<unsigned>(result),rollback_completed?"true":"false",
        cpu_initialized?"vm":"cpu-initialize");
    CHECK(!std::fclose(f));
}

// Call only after the core's VM lifecycle and CPUThreadShutdown have completed.
// Startup failure may have opened no devices; ownership checks still apply.
static Ps2HbxlVmAvStatistics finish_frontend_resources(){
    CHECK(!ps2_hbxl_vm_av_shutdown());
    Ps2HbxlVmAvStatistics av{};ps2_hbxl_vm_av_statistics(&av);
    CHECK(!av.error && av.workers==av.joins && av.audio_opens==av.audio_closes &&
        (!av.workers || av.private_worker_tls_checked) && av.gs_joined && av.queue_idle && !av.retained && av.shutdown);
    FILE *av_file=std::fopen("hbxl-vm-av.json","wb");CHECK(av_file);
    std::fprintf(av_file,"{\"passed\":true,\"presented\":%llu,\"audioAppends\":%llu,\"audioOpens\":%llu,\"audioCloses\":%llu,\"workers\":%llu,\"joins\":%llu,\"privateMuslWorkerTlsChecked\":%s,\"gsJoined\":true,\"queueIdle\":true,\"retained\":false}\n",
        (unsigned long long)av.presented,(unsigned long long)av.audio_appends,(unsigned long long)av.audio_opens,
        (unsigned long long)av.audio_closes,(unsigned long long)av.workers,(unsigned long long)av.joins,
        av.private_worker_tls_checked?"true":"false");CHECK(!std::fclose(av_file));
    ps2_hbxl_vm_fs_shutdown();
#ifdef PS2_NATIVE_SD_SAVES
    ps2_sd_saves_native_shutdown();
#endif
    ps2_hbxl_vm_memory_shutdown();return av;
}


namespace {
std::mutex controls_mutex;
ps2::SessionControl* controls = nullptr;
std::atomic<uint64_t> presented_cache{0};
std::deque<uint64_t> published_ids;
std::deque<ps2::RequestStatus> terminal_receipts;
ps2::SessionSnapshot terminal_snapshot;
bool terminal_available=false;
#ifdef PS2_SESSION_VERIFICATION
std::atomic<uint64_t> verification_frame_baseline{UINT64_MAX};
struct GuestSnapshot { u32 pc; u64 ee_cycle,iop_cycle,ram; };
GuestSnapshot guest_snapshot(){
    u64 h=1469598103934665603ull;
    for(size_t i=0;i<Ps2MemSize::MainRam;i++){h^=eeMem->Main[i];h*=1099511628211ull;}
    return {cpuRegs.pc,cpuRegs.cycle,psxRegs.cycle,h};
}
bool same_guest(const GuestSnapshot&a,const GuestSnapshot&b){
    return a.pc==b.pc&&a.ee_cycle==b.ee_cycle&&a.iop_cycle==b.iop_cycle&&a.ram==b.ram;
}
#endif
}
#ifdef PS2_SESSION_VERIFICATION
extern "C" uint64_t ps2_session_verification_frame_baseline(){return verification_frame_baseline.load(std::memory_order_acquire);}
extern "C" int ps2_hbxl_vm_av_model_capture_final(void);
extern "C" void ps2_hbxl_vm_av_model_readiness_diagnostics(void);
#endif
extern "C" uint64_t ps2_session_presented_frames(){return presented_cache.load(std::memory_order_acquire);}
extern "C" uint64_t ps2_session_request(unsigned operation,uint64_t revision,const char*checkpoint){
    if(operation>PS2_SESSION_LOAD)return 0;
    std::string path;if(checkpoint){size_t n=0;while(n<=ps2::SessionControl::kMaxPathBytes&&checkpoint[n])n++;if(n>ps2::SessionControl::kMaxPathBytes)return 0;path.assign(checkpoint,n);}
    std::lock_guard<std::mutex> lock(controls_mutex);
    if(!controls)return 0;const auto id=controls->Submit(static_cast<ps2::Operation>(operation),revision,std::move(path)).id;
    if(id){published_ids.push_back(id);if(published_ids.size()>ps2::SessionControl::kHistoryCapacity)published_ids.pop_front();}return id;
}
extern "C" int ps2_session_status(uint64_t id,Ps2SessionStatus*out){
    if(!out)return 0;std::lock_guard<std::mutex> lock(controls_mutex);if(!controls&&!terminal_available)return 0;
    const auto s=controls?controls->Snapshot():terminal_snapshot;ps2::RequestStatus r;
    if(controls)r=controls->Inspect(id);else for(const auto& receipt:terminal_receipts)if(receipt.id==id){r=receipt;break;}
    *out={s.revision,id,s.queued,s.executing,static_cast<uint32_t>(s.state),static_cast<uint32_t>(r.outcome),static_cast<uint32_t>(r.reason),s.unresolved?1u:0u};return 1;
}

int main(int argc,char**argv){
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stderr,nullptr,_IONBF,0);
    ps2_hbxl_vm_stage("main-enter");const auto *config=ps2_hbxl_vm_configuration(argc,argv);CHECK(ps2_hbxl_vm_validate_configuration(config));
    const bool bios_only=(config->flags&PS2_HBXL_VM_FLAG_BIOS_ONLY)!=0;
    const char* requested_resume=std::getenv("PS2_RESUME_STATE");
    const bool explicit_resume=requested_resume&&*requested_resume;
    CHECK(!bios_only||!explicit_resume);
    char *paths[]={const_cast<char*>("ps2-hbxl-full-vm-no-av"),const_cast<char*>(config->scratch),const_cast<char*>(config->resources),const_cast<char*>(config->bios_filename),const_cast<char*>(config->iso),const_cast<char*>(config->anchor)};argv=paths;argc=6;
    ps2_hbxl_vm_memory_initialize();ps2_hbxl_vm_stage("native-av-initialize-before");const int av_init=ps2_hbxl_vm_av_initialize();if(av_init)std::fprintf(stderr,"HBXL VM AV initialize error %d\n",av_init);CHECK(!av_init);CHECK(std::getenv("PS2_HEADLESS_SW") && !std::strcmp(std::getenv("PS2_HEADLESS_SW"),"1"));Log::SetConsoleOutputLevel(LOGLEVEL_INFO);
#ifdef PS2_CAPTURE_SW_FRAME
    ps2_check_image_codec_abi();
#endif
    EmuFolders::AppRoot=argv[1];EmuFolders::DataRoot=argv[1];EmuFolders::Resources=argv[2];
    EmuFolders::Settings=EmuFolders::DataRoot+"/inis";
    CHECK(FileSystem::EnsureDirectoryExists(EmuFolders::Settings.c_str(),false));
    ps2_hbxl_vm_fs_initialize(argv[1],argv[2],argv[4]);
#ifdef PS2_NATIVE_SD_SAVES
    // A save-enabled native image must not silently acknowledge PC-only saves
    // when mounting or validating the SD filesystem failed.
    CHECK(ps2_sd_saves_native_initialize());
#endif
    // Upstream installs these layers once for the process lifetime.
    static MemorySettingsInterface settings,secrets;
    Host::Internal::SetBaseSettingsLayer(&settings);Host::Internal::SetSecretsSettingsLayer(&secrets);
    VMManager::SetDefaultSettings(settings,true,true,true,true,true);
    ps2_hbxl_vm_stage("cpu-default-settings-complete");
    // The SDK mask gives available processors, not physical clusters or tiers.
    settings.SetBoolValue("EmuCore","EnableThreadPinning",false);
    settings.SetStringValue("Filenames","BIOS",argv[3]);
    settings.SetIntValue("EmuCore/GS","Renderer",static_cast<int>(
#ifdef PS2_CAPTURE_SW_FRAME
        GSRendererType::SW
#else
        GSRendererType::Null
#endif
    ));
#ifdef PS2_CAPTURE_SW_FRAME
    settings.SetIntValue("EmuCore/GS","extrathreads",0);
    settings.SetBoolValue("EmuCore/GS","GSBackThreadMode",false);
#endif
    settings.SetBoolValue("InputSources","SDL",false);
    settings.SetStringValue("SPU2/Output","Backend","Null");
    settings.SetBoolValue("EmuCore/Speedhacks","vuThread",false);
    settings.SetBoolValue("EmuCore/Speedhacks","iopThread",false);
    settings.SetBoolValue("EmuCore/CPU/Recompiler","EnableVUAot",false);
    settings.SetBoolValue("EmuCore/CPU/Recompiler","EnableVUProgramCache",false);
    settings.SetBoolValue("Achievements","Enabled",false);
    settings.SetBoolValue("EmuCore","EnablePINE",false);
    settings.SetBoolValue("EmuCore","EnableDiscordPresence",false);
    settings.SetBoolValue("Logging","EnableSystemConsole",true);
    const bool card_experiment=std::getenv("PS2_CARD_EXPERIMENT")!=nullptr;
    const char* card_filename=card_experiment?"probe-slot1.ps2":"user-slot1.ps2";
    settings.SetBoolValue("MemoryCards","Slot1_Enable",true);
    settings.SetStringValue("MemoryCards","Slot1_Filename",card_filename);
    settings.SetBoolValue("MemoryCards","AutoBackup",!card_experiment);
    settings.SetBoolValue("MemoryCards","Slot2_Enable",false);
    // CPUThreadInitialize reserves microVU before it calls LoadSettings.
    // Apply the same persisted recompiler settings before Reserve can start
    // the optional AOT worker from the global default (EnableVUAot=true).
    // Only these early optional-worker flags are needed here. Loading all
    // recompiler settings would overwrite the platform's fastmem capability
    // decision made by ps2_hbxl_vm_memory_initialize above.
    EmuConfig.Cpu.Recompiler.EnableVUAot=settings.GetBoolValue("EmuCore/CPU/Recompiler","EnableVUAot",false);
    EmuConfig.Cpu.Recompiler.EnableVUProgramCache=settings.GetBoolValue("EmuCore/CPU/Recompiler","EnableVUProgramCache",false);
    CHECK(!EmuConfig.Cpu.Recompiler.EnableVUAot);
    CHECK(!EmuConfig.Cpu.Recompiler.EnableVUProgramCache);
    CHECK(EmuConfig.Cpu.Recompiler.EnableEE && EmuConfig.Cpu.Recompiler.EnableIOP &&
          EmuConfig.Cpu.Recompiler.EnableVU0 && EmuConfig.Cpu.Recompiler.EnableVU1);
    ps2_hbxl_vm_stage("startup-recompiler-settings-applied");
    VMManager::Internal::LoadStartupSettings();
#ifdef PS2_NATIVE_SD_SAVES
    CHECK(FileSystem::EnsureDirectoryExists(EmuFolders::MemoryCards.c_str(),false));
    const std::string sd_card_path=EmuFolders::MemoryCards+"/"+card_filename;
    ps2::SdSaveResult sd_card_restore;
#ifdef PS2_NATIVE_SD_LIVE_CARD
    sd_card_restore=ps2::RestoreLiveSdCard(sd_card_path,card_filename);
    const bool existing_live_card=sd_card_restore.success&&sd_card_restore.found;
    if(sd_card_restore.success&&!sd_card_restore.found)
#endif
        sd_card_restore=ps2::RestoreSdSave(sd_card_path,card_filename);
    if(!sd_card_restore.success)std::fprintf(stderr,"SD memory card restore: %s\n",sd_card_restore.error.c_str());
    CHECK(sd_card_restore.success);
#endif
    const auto private_card=ps2::PreparePrivateSlot1Card(config->scratch,card_filename);
    if(!private_card.success)std::fprintf(stderr,"Private memory card: %s\n",private_card.error.c_str());
    CHECK(private_card.success);
#ifdef PS2_NATIVE_SD_SAVES
    if(!sd_card_restore.found){
        const auto first_card=ps2::StoreSdSave(sd_card_path,card_filename);
        if(!first_card.success)std::fprintf(stderr,"SD memory card creation: %s\n",first_card.error.c_str());
        CHECK(first_card.success);
    }
#ifdef PS2_NATIVE_SD_LIVE_CARD
    if(!existing_live_card){
        const auto imported=ps2::CreateLiveSdCard(sd_card_path,card_filename);
        if(!imported.success)std::fprintf(stderr,"SD live card import: %s\n",imported.error.c_str());
        CHECK(imported.success);
    }
    CHECK(ps2::BindLiveSdCard(sd_card_path,card_filename));
    std::printf("{\"sdLiveCardStartup\":true,\"existingRawCard\":%s,\"rawCardAuthoritative\":true,\"writesAtGameTime\":true}\n",existing_live_card?"true":"false");
    std::fflush(stdout);
#else
    const std::string& sd_card_snapshot_path=sd_card_path;
#endif
#endif
    const char* hardware_error=nullptr;CHECK(VMManager::PerformEarlyHardwareChecks(&hardware_error));
    ps2_hbxl_vm_stage("cpu-thread-initialize-before");
    if(!VMManager::Internal::CPUThreadInitialize()){
        CHECK(VMManager::GetState()==VMState::Shutdown);
        // SysMemory::Allocate already rolls back a partial memory image.
        // Providers have not been started; shutdown joins/releases anything
        // established before the failure without inventing a live VM.
        ps2_hbxl_vm_stage("cpu-initialize-failed-cleanup-before");
        VMManager::Internal::CPUThreadShutdown();vu1Thread.Close();
        finish_frontend_resources();record_startup(VMBootResult::StartupFailure,true,false);
        ps2_hbxl_vm_stage("cpu-initialize-failed-clean-shutdown");return 0;
    }
    VMBootParameters boot;boot.filename=argv[4];boot.fast_boot=!bios_only;
    if(bios_only)boot.source_type=CDVD_SourceType::NoDisc;
    boot.start_unlimited=
#ifdef PS2_SESSION_VERIFICATION
        true;
#else
        false;
#endif
    if(explicit_resume)boot.save_state=requested_resume;
#ifdef PS2_SPRITE_ORACLE
    CHECK(!bios_only);
    CHECK(std::getenv("PS2_ORACLE_ELF"));boot.elf_override=std::getenv("PS2_ORACLE_ELF");
#endif
    ps2_hbxl_vm_stage("vm-initialize-before");Error error;const auto result=VMManager::Initialize(boot,&error);
    if(result!=VMBootResult::StartupSuccess)std::fprintf(stderr,"VM boot: %s\n",error.GetDescription().c_str());
    if(result!=VMBootResult::StartupSuccess){
        // Initialize's scoped guards already closed any opened VM devices.
        // Shutdown requires a live VM and must not run again on this path.
        CHECK(VMManager::GetState()==VMState::Shutdown);
        ps2_hbxl_vm_stage("startup-rollback-cpu-before");
        VMManager::Internal::CPUThreadShutdown();vu1Thread.Close();
        finish_frontend_resources();record_startup(result,true);
        ps2_hbxl_vm_stage("startup-failed-clean-shutdown");return 0;
    }
    record_startup(result,false);
    ps2_hbxl_vm_stage("vm-initialize-complete");
    CHECK(Cpu==&recCpu && psxCpu==&psxRec && !EmuConfig.Speedhacks.vuThread && !EmuConfig.Speedhacks.iopThread);
    auto shutdown_vm=[&](){
    ps2_hbxl_vm_stage("vm-shutdown-before");VMManager::Shutdown(false);
    VMManager::Internal::CPUThreadShutdown();vu1Thread.Close();
#ifdef PS2_NATIVE_SD_SAVES
    // Upstream Close can update the raw card checksum during Shutdown.
#ifdef PS2_NATIVE_SD_LIVE_CARD
    // VM Shutdown already closes and flushes the authoritative live raw card.
    // Quick exit must not start a second full-card backup or a savestate.
    CHECK(ps2::LiveSdCardHealthy());
    if(!quick_exit_requested()){
        const auto final_card=ps2::SnapshotLiveSdCard();
        if(!final_card.success)std::fprintf(stderr,"Final SD memory card: %s\n",final_card.error.c_str());
        CHECK(final_card.success);
    }
#else
    const auto final_card=ps2::StoreSdSave(sd_card_path,card_filename);
    if(!final_card.success)std::fprintf(stderr,"Final SD memory card: %s\n",final_card.error.c_str());
    CHECK(final_card.success);
#endif
#endif
    ps2_hbxl_vm_stage("vm-shutdown-complete");return finish_frontend_resources();
    };
    VMManager::SetState(VMState::Running);
    u64 consumed=0;unsigned batches=0;const auto start=std::chrono::steady_clock::now();
    const bool initial_has_booted_elf=VMManager::Internal::HasBootedELF();
    const u32 initial_guest_pc=cpuRegs.pc;
    const u64 initial_ee_cycle=cpuRegs.cycle,initial_iop_cycle=psxRegs.cycle;
    bool elf_entry_transition_observed=false;
    if(bios_only)CHECK(!initial_has_booted_elf&&initial_guest_pc==0xbfc00000u);
    if(!quick_exit_requested()&&!bios_only&&VMManager::Internal::HasBootedELF()){
        const s32 cycles=recEeExecuteBlock(1<<20,0xffffffffu);CHECK(cycles>0);consumed+=cycles;batches++;
    }
    // Production boot duration depends on the game and console speed. Probe
    // budgets belong only to verification builds; reaching one must not turn
    // an ordinary slow game boot into the fatal-retention path.
    while(!quick_exit_requested()&&!bios_only&&!VMManager::Internal::HasBootedELF()
#ifdef PS2_SESSION_VERIFICATION
          && batches<2000
#endif
    ){
        const s32 cycles=recEeExecuteBlock(1<<20,0xffffffffu);CHECK(cycles>0);consumed+=cycles;batches++;
        if(VMManager::Internal::HasBootedELF())elf_entry_transition_observed=true;
        for(auto&task:StubHost::TakeCpuThreadTasks())task.function();
        if((batches&31)==0)std::fprintf(stderr,"VM progress: batches=%u EE=%llu pc=%08x IOP=%08x\n",batches,(unsigned long long)consumed,cpuRegs.pc,psxRegs.pc);
#ifdef PS2_SESSION_VERIFICATION
        CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(150));
#endif
    }
    if(quick_exit_requested()){
        Pad::ResetAllControllerInputs();VMManager::SetState(VMState::Paused);
        shutdown_vm();record_quick_exit(true);
        ps2_hbxl_vm_stage("main-boot-cancelled-clean-shutdown");return 0;
    }
    CHECK(bios_only||VMManager::Internal::HasBootedELF());
    const bool guest_elf_state_available=VMManager::Internal::HasBootedELF();
    const bool bios_boot_observed=!explicit_resume&&!initial_has_booted_elf&&elf_entry_transition_observed;
    ps2_hbxl_vm_stage(bios_only?"bios-no-disc-start":elf_entry_transition_observed?"elf-entry-observed":"restored-elf-state-observed");
    const u32 entry=bios_only?0u:VMManager::Internal::GetCurrentELFEntryPoint();
#ifdef PS2_SPRITE_ORACLE
    CHECK(entry==0x100000);
#else
    CHECK(bios_only||entry<Ps2MemSize::MainRam-256);
#endif
    // A game-independent application cannot require a captured copy of the
    // guest's entry bytes. Keep the explicit diagnostic oracle when supplied.
    if(config->anchor[0]){
        const auto anchor=FileSystem::ReadBinaryFile(argv[5]);CHECK(anchor && anchor->size()==256);
        CHECK(!std::memcmp(eeMem->Main+entry,anchor->data(),anchor->size()));
    }
    // BiosChecksum is the core's XOR32 ROM checksum, not a cryptographic hash.
    // A distinct namespace prevents ordinary BIOS saves from selecting a game's
    // default autosave. BIOS entries always start from reset unless manually loaded.
    const std::string game_identity=bios_only?"bios:"+std::to_string(BiosChecksum)+":"+BiosSerial:
        VMManager::GetDiscSerial()+":"+std::to_string(VMManager::GetDiscCRC());
    const auto autosave_paths=ps2::GameAutosavePaths(config->scratch,game_identity);
    CHECK(autosave_paths.valid);
    auto reset_scanout_after_load=[](){
        MTGS::RunOnGSThread([](){ps2_sw_scanout_reset_history();});MTGS::WaitGS();
    };
    bool startup_restore_failed=false;
#ifdef PS2_NATIVE_SD_SAVES
    bool sd_startup_state_found=false,sd_startup_state_loaded=false;
    uint64_t sd_startup_state_generation=0;
    if(!bios_only&&!explicit_resume){
        const auto restored=ps2::RestoreSdSave(autosave_paths.current,autosave_paths.current_name);
        sd_startup_state_found=restored.found;sd_startup_state_generation=restored.generation;
        if(!restored.success){std::fprintf(stderr,"SD autosave restore: %s\n",restored.error.c_str());startup_restore_failed=true;}
    }
#endif
    if(!bios_only&&!explicit_resume&&!startup_restore_failed){
        const auto available=ps2::FindGameAutosave(config->scratch,game_identity,"");
        if(available.found){
            const auto restored=ps2::LoadGameAutosave(config->scratch,game_identity,"");
            if(restored.success||restored.guest_state_may_have_changed)reset_scanout_after_load();
#ifdef PS2_NATIVE_SD_SAVES
            sd_startup_state_loaded=restored.success&&sd_startup_state_found;
#endif
            if(!restored.success)std::fprintf(stderr,"Automatic restore: %s\n",restored.error.c_str());
            CHECK(restored.state_restored);
            startup_restore_failed=!restored.success;
        }else if(!available.error.empty()){
            std::fprintf(stderr,"Automatic restore: %s\n",available.error.c_str());
            startup_restore_failed=true;
        }
    }
#ifdef PS2_NATIVE_SD_SAVES
    std::printf("{\"sdSaveStartup\":true,\"cardFound\":%s,\"cardGeneration\":%llu,\"stateFound\":%s,\"stateGeneration\":%llu,\"stateLoadedIntoVm\":%s,\"startupRestoreFailed\":%s}\n",
        sd_card_restore.found?"true":"false",(unsigned long long)sd_card_restore.generation,
        sd_startup_state_found?"true":"false",(unsigned long long)sd_startup_state_generation,
        sd_startup_state_loaded?"true":"false",startup_restore_failed?"true":"false");
    std::fflush(stdout);
#endif
    // A corrupt archive may have reset guest contents. Keep the usable VM
    // paused and let the user deliberately resume/reboot or retry the backup.
    if(startup_restore_failed)VMManager::SetState(VMState::Paused);
    const u32 final_pc=cpuRegs.pc;const u64 iop_cycles=psxRegs.cycle;
    CHECK(bios_only||(consumed>0 && iop_cycles>0));
    ps2_hbxl_vm_stage("native-av-run-before");
    bool av_ready=false;
    ps2::SessionControl session(startup_restore_failed?ps2::SessionState::Paused:ps2::SessionState::Running);
    {std::lock_guard<std::mutex> lock(controls_mutex);CHECK(!controls);published_ids.clear();terminal_receipts.clear();terminal_available=false;controls=&session;}
    ps2::SessionCallbacks callbacks;
    auto persist_sd=[&](const std::string& checkpoint_path,const std::string& key){
#ifdef PS2_NATIVE_SD_SAVES
        const VMState before=VMManager::GetState();
        if(before==VMState::Running)VMManager::SetState(VMState::Paused);
        MTGS::WaitGS();
        bool ok=ps2_checked_memcard_flush();
#ifdef PS2_NATIVE_SD_LIVE_CARD
        ok=ok&&ps2::LiveSdCardHealthy();
        if(ok){const auto card=ps2::SnapshotLiveSdCard();ok=card.success;
            if(!ok)std::fprintf(stderr,"SD live card snapshot: %s\n",card.error.c_str());}
#else
        if(ok){const auto card=ps2::StoreSdSave(sd_card_snapshot_path,card_filename);ok=card.success;
            if(!ok)std::fprintf(stderr,"SD memory card: %s\n",card.error.c_str());}
#endif
        if(ok&&!checkpoint_path.empty()){const auto state=ps2::StoreSdSave(checkpoint_path,key);ok=state.success;
            if(!ok)std::fprintf(stderr,"SD checkpoint: %s\n",state.error.c_str());}
        if(before==VMState::Running)VMManager::SetState(VMState::Running);
        return ok;
#else
        (void)checkpoint_path;(void)key;return true;
#endif
    };
    callbacks.pause=[&](){VMManager::SetState(VMState::Paused);
        if(VMManager::GetState()!=VMState::Paused)return ps2::CallbackResult::Unknown;
        if(quick_exit_requested())return ps2::CallbackResult::Succeeded;
        return persist_sd("","")?ps2::CallbackResult::Succeeded:ps2::CallbackResult::SucceededWithWarning;};
    callbacks.resume=[&](){
        if(bios_only&&VMManager::Internal::HasBootedELF())return ps2::CallbackResult::FailedNoChange;
        VMManager::SetState(VMState::Running);return VMManager::GetState()==VMState::Running?ps2::CallbackResult::Succeeded:ps2::CallbackResult::Unknown;};
    callbacks.stop=[&](){
        if(quick_exit_requested()){
            if(VMManager::GetState()==VMState::Running)VMManager::SetState(VMState::Paused);
            VMManager::SetState(VMState::Stopping);
            return VMManager::GetState()==VMState::Stopping?ps2::CallbackResult::Succeeded:ps2::CallbackResult::Unknown;
        }
        if(bios_only&&VMManager::Internal::HasBootedELF())return ps2::CallbackResult::FailedNoChange;
        const auto saved=ps2::SaveGameAutosave(config->scratch,game_identity);
        if(!saved.success){
            std::fprintf(stderr,"Save and exit: %s\n",saved.error.c_str());
            return saved.state_restored?ps2::CallbackResult::FailedNoChange:ps2::CallbackResult::Unknown;
        }
        if(!persist_sd(autosave_paths.current,autosave_paths.current_name))return ps2::CallbackResult::FailedNoChange;
        if(VMManager::GetState()==VMState::Running)VMManager::SetState(VMState::Paused);
        VMManager::SetState(VMState::Stopping);
        return VMManager::GetState()==VMState::Stopping?ps2::CallbackResult::Succeeded:ps2::CallbackResult::Unknown;
    };
    ps2::CheckpointResult checkpoint_result;
    auto checkpoint=[&](bool load,const std::string& path){
#ifdef PS2_NATIVE_SD_SAVES
        const std::string key=path.substr(path.rfind('/')==std::string::npos?0:path.rfind('/')+1);
        const std::string local=config->scratch+std::string("/")+key;
        if((path!=key&&path!=local)||key.size()<5||key.compare(key.size()-4,4,".p2s"))return ps2::CallbackResult::FailedNoChange;
        if(load){const auto restored=ps2::RestoreRequiredSdSave(local,key);if(!restored.success){std::fprintf(stderr,"SD load: %s\n",restored.error.c_str());return ps2::CallbackResult::FailedNoChange;}}
#endif
        checkpoint_result=load?ps2::LoadCheckpoint(config->scratch,path):ps2::SaveCheckpoint(config->scratch,path);
        if(!load&&checkpoint_result.success){
            const std::string key=checkpoint_result.path.substr(checkpoint_result.path.rfind('/')+1);
            if(!persist_sd(checkpoint_result.path,key))return ps2::CallbackResult::FailedNoChange;
        }
        if(load&&(checkpoint_result.success||checkpoint_result.guest_state_may_have_changed))reset_scanout_after_load();
        if(checkpoint_result.success&&checkpoint_result.state_restored)return ps2::CallbackResult::Succeeded;
        std::fprintf(stderr,"Checkpoint: %s\n",checkpoint_result.error.c_str());
        if(!checkpoint_result.state_restored)return ps2::CallbackResult::Unknown;
        return checkpoint_result.guest_state_may_have_changed?ps2::CallbackResult::FailedChanged:ps2::CallbackResult::FailedNoChange;};
    callbacks.save=[&](const std::string&path){
        if(bios_only&&path!=autosave_paths.current_name)return ps2::CallbackResult::FailedNoChange;
        if(bios_only&&VMManager::Internal::HasBootedELF())return ps2::CallbackResult::FailedNoChange;
        if(path!=autosave_paths.current_name)return checkpoint(false,path);
        const auto saved=ps2::SaveGameAutosave(config->scratch,game_identity);
        if(saved.success&&!persist_sd(autosave_paths.current,autosave_paths.current_name))return ps2::CallbackResult::FailedNoChange;
        if(saved.success&&saved.state_restored)return ps2::CallbackResult::Succeeded;
        return saved.state_restored?ps2::CallbackResult::FailedNoChange:ps2::CallbackResult::Unknown;
    };
    callbacks.load=[&](const std::string&path){
        if(bios_only&&path!=autosave_paths.current_name)return ps2::CallbackResult::FailedNoChange;
        if(path!=autosave_paths.current_name)return checkpoint(true,path);
#ifdef PS2_NATIVE_SD_SAVES
        const auto sd_restore=ps2::RestoreRequiredSdSave(autosave_paths.current,autosave_paths.current_name);
        if(!sd_restore.success){std::fprintf(stderr,"SD autosave load: %s\n",sd_restore.error.c_str());return ps2::CallbackResult::FailedNoChange;}
#endif
        const auto loaded=ps2::LoadGameAutosave(config->scratch,game_identity,"");
        if(loaded.success||loaded.guest_state_may_have_changed)reset_scanout_after_load();
        if(bios_only&&loaded.success&&VMManager::Internal::HasBootedELF())return ps2::CallbackResult::FailedChanged;
        if(loaded.success&&loaded.state_restored)return ps2::CallbackResult::Succeeded;
        if(!loaded.state_restored)return ps2::CallbackResult::Unknown;
        return loaded.guest_state_may_have_changed?ps2::CallbackResult::FailedChanged:ps2::CallbackResult::FailedNoChange;
    };
    struct MenuHost { bool dirty=false; } menu_host;
    ps2::AppControllerHost app_host{};app_host.context=&menu_host;
    app_host.poll_input=[](void*,ps2::AppControllerInput*out){
        Ps2NativePadStatistics input{};
        if(ps2_native_pad_statistics(&input)||!input.initialized){*out={};return true;}
        if(ps2_native_pad_poll()||ps2_native_pad_statistics(&input)){*out={};return false;}
        *out={input.frame.buttons,input.frame.connected!=0};return true;
    };
    app_host.suppress_gameplay_input=[](void*){Pad::ResetAllControllerInputs();};
    app_host.request_quit=[](void*){__atomic_store_n(&ps2_exit_requested,1u,__ATOMIC_RELEASE);};
    app_host.publish_view=[](void*context,const ps2::AppControllerView*v){
        const Ps2AppMenuView view{v->visible?1u:0u,v->selected,v->waiting?1u:0u,v->confirming?1u:0u,static_cast<unsigned>(v->message)};
        ps2_app_menu_publish(&view);static_cast<MenuHost*>(context)->dirty=true;
    };
    ps2::AppController app_controller(app_host,{autosave_paths.current_name.c_str(),600,
        startup_restore_failed?ps2::AppControllerMessage::Failed:ps2::AppControllerMessage::None});
    CHECK(app_controller.Enabled());
#ifdef PS2_SESSION_VERIFICATION
    // Drain prior GS work before the epoch: ELF entry for a game, or reset for BIOS.
    // Public presented/cache counters remain absolute and are never rewritten.
    MTGS::WaitGS();Ps2HbxlVmAvStatistics initial_av{};ps2_hbxl_vm_av_statistics(&initial_av);
    const uint64_t frame_baseline=initial_av.presented;
    verification_frame_baseline.store(frame_baseline,std::memory_order_release);
    const unsigned target=std::getenv("PS2_SESSION_FRAMES")?std::strtoul(std::getenv("PS2_SESSION_FRAMES"),nullptr,10):120;
    const unsigned seconds=std::getenv("PS2_SESSION_MAX_SECONDS")?std::strtoul(std::getenv("PS2_SESSION_MAX_SECONDS"),nullptr,10):180;
    CHECK(target>=100&&target<=3600&&seconds>=10&&seconds<=600);
    unsigned commands=0,resumes=0,saves=0,loads=0;bool paused_verified=false,restored_exact=false,advanced_after_load=false;
    bool verification_ok=false;
    GuestSnapshot saved{};u64 last_progress=0,verified_frames=0;
    const std::string verification_checkpoint=bios_only?autosave_paths.current_name:"session-checkpoint.p2s";
    auto command=[&](ps2::Operation op,const std::string&path=std::string{}){const auto id=ps2_session_request(static_cast<unsigned>(op),session.Snapshot().revision,path.empty()?nullptr:path.c_str());CHECK(id&&session.Inspect(id).outcome==ps2::Outcome::Queued);CHECK(session.ProcessOne(callbacks)==ps2::ProcessResult::Completed);CHECK(session.Inspect(id).outcome==ps2::Outcome::Succeeded);commands++;};
#endif
    uint64_t last_health_ms=0;
    while(session.Snapshot().state!=ps2::SessionState::Stopped){
        for(auto&task:StubHost::TakeCpuThreadTasks())task.function();
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        if(quick_exit_requested())app_controller.RequestExit();
        app_controller.Tick(static_cast<uint64_t>(now));
        ps2_native_pad_set_input_owned(app_controller.ClaimsInput()?1u:0u);
        const auto processed=session.ProcessOne(callbacks);CHECK(processed!=ps2::ProcessResult::WrongThread);
        const auto current=session.Snapshot();if(current.state==ps2::SessionState::Stopped)break;
        if(menu_host.dirty){
            int refreshed=0;MTGS::RunOnGSThread([&](){refreshed=ps2_hbxl_vm_av_refresh();});MTGS::WaitGS();
            CHECK(!refreshed||refreshed==PS2_SCANOUT_NO_DISPLAY);menu_host.dirty=false;
        }
        if(current.unresolved){
            // Retain the receipt and all resources until the CPU adapter can
            // establish its outcome. Never execute a guest slice on uncertainty.
            if(VMManager::GetState()==VMState::Running)VMManager::SetState(VMState::Paused);
            Pad::ResetAllControllerInputs();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;
        }
        if(current.state==ps2::SessionState::Paused){std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
        const auto running_state=VMManager::GetState();const int av_error=ps2_hbxl_vm_av_error();
        if(running_state!=VMState::Running||av_error){
            Ps2HbxlVmAvStatistics failure_av{};ps2_hbxl_vm_av_statistics(&failure_av);
            std::fprintf(stderr,"PS2_SESSION_RUN_FAILURE state=%u avError=%d presented=%llu audioAppends=%llu retained=%u\n",
                static_cast<unsigned>(running_state),av_error,(unsigned long long)failure_av.presented,
                (unsigned long long)failure_av.audio_appends,failure_av.retained);
        }
        CHECK(running_state==VMState::Running&&!av_error);
        CHECK(!bios_only||!VMManager::Internal::HasBootedELF());
        const auto cycles=recEeExecuteBlock(1<<20,0xffffffffu);CHECK(cycles>0);consumed+=cycles;batches++;
        Ps2HbxlVmAvStatistics current_av{};ps2_hbxl_vm_av_statistics(&current_av);presented_cache.store(current_av.presented,std::memory_order_release);
        const u64 frames=current_av.presented;av_ready=av_ready||ps2_hbxl_vm_av_ready();
        if(static_cast<uint64_t>(now)>=last_health_ms+1000){
            last_health_ms=static_cast<uint64_t>(now);
            Ps2HbxlMemorySnapshot memory{};hbxl_memory_snapshot(&memory);
            Ps2SdkThreadStatistics threads{};ps2_sdk_threads_statistics(&threads);
            std::fprintf(stderr,"PS2_RUNTIME_HEALTH {\"frames\":%llu,\"pc\":%u,\"audioAppends\":%llu,\"avError\":%u,\"threadsCreated\":%llu,\"threadsJoined\":%llu,\"sdkReserved\":%llu,\"sdkUsed\":%llu,\"sdkLargestFree\":%llu,\"vkReserved\":%llu,\"vkUsed\":%llu,\"sdkFailedRequests\":%llu,\"sdkLastRequest\":%llu,\"sdkLastResult\":%d,\"stackScopes\":%u,\"poisoned\":%u}\n",
                (unsigned long long)frames,cpuRegs.pc,(unsigned long long)current_av.audio_appends,current_av.error,
                (unsigned long long)threads.created,(unsigned long long)threads.joined,
                (unsigned long long)memory.sdk_reserved,(unsigned long long)memory.sdk_used,(unsigned long long)memory.sdk_largest_free,
                (unsigned long long)memory.vk_reserved,(unsigned long long)memory.vk_used,
                (unsigned long long)memory.sdk_failed_requests,(unsigned long long)memory.sdk_last_request,memory.sdk_last_result,memory.stack_scopes,memory.poisoned);
            std::fflush(stderr);
        }
#ifdef PS2_SESSION_VERIFICATION
        CHECK(frames>=frame_baseline);verified_frames=frames-frame_baseline;
        if(verified_frames>=20&&!paused_verified){
            command(ps2::Operation::Pause);const auto paused=guest_snapshot();
            const Ps2AppMenuView preview{1,0,0,0,0};ps2_app_menu_publish(&preview);
            int refreshed=0;MTGS::RunOnGSThread([&](){refreshed=ps2_hbxl_vm_av_refresh();});MTGS::WaitGS();CHECK(!refreshed);
            for(unsigned i=0;i<16;i++){CHECK(session.ProcessOne(callbacks)==ps2::ProcessResult::Idle);CHECK(cpuRegs.pc==paused.pc&&cpuRegs.cycle==paused.ee_cycle&&psxRegs.cycle==paused.iop_cycle);}
            CHECK(same_guest(paused,guest_snapshot()));paused_verified=true;
            ps2_app_menu_publish(nullptr);MTGS::RunOnGSThread([&](){refreshed=ps2_hbxl_vm_av_refresh();});MTGS::WaitGS();CHECK(!refreshed);
            command(ps2::Operation::Resume);resumes++;
        }
        if(verified_frames>=40&&!saves){command(ps2::Operation::Pause);command(ps2::Operation::Save,verification_checkpoint);saved=guest_snapshot();saves++;command(ps2::Operation::Resume);resumes++;}
        if(verified_frames>=80&&!loads){CHECK(saves&&guest_snapshot().ram!=saved.ram);command(ps2::Operation::Pause);command(ps2::Operation::Load,verification_checkpoint);restored_exact=same_guest(saved,guest_snapshot());CHECK(restored_exact);loads++;command(ps2::Operation::Resume);resumes++;}
        if(loads&&verified_frames>=100&&!advanced_after_load)advanced_after_load=guest_snapshot().ram!=saved.ram;
        if(verified_frames>=target){
            verification_ok=av_ready&&paused_verified&&saves==1&&loads==1&&restored_exact&&advanced_after_load;
            std::fprintf(stderr,"PS2_SESSION_VERIFICATION_RESULT passed=%u avReady=%u paused=%u saves=%u loads=%u restoredExact=%u advancedAfterLoad=%u presented=%llu appends=%llu tls=%u avError=%u\n",
                verification_ok?1u:0u,av_ready?1u:0u,paused_verified?1u:0u,saves,loads,
                restored_exact?1u:0u,advanced_after_load?1u:0u,(unsigned long long)frames,
                (unsigned long long)current_av.audio_appends,current_av.private_worker_tls_checked,current_av.error);
            ps2_hbxl_vm_av_model_readiness_diagnostics();
            // A failed qualification still stops and drains normally, preserving
            // final pixels/PCM and real cleanup receipts for diagnosis.
            command(ps2::Operation::Stop);
        }
        if(verified_frames>=last_progress+30){last_progress=verified_frames;std::fprintf(stderr,"PS2 session relativeFrames=%llu absoluteFrames=%llu commands=%u\n",(unsigned long long)verified_frames,(unsigned long long)frames,commands);}
        if(session.Snapshot().state!=ps2::SessionState::Stopped)
            CHECK(std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds));
#else
        (void)frames;
#endif
    }
    const u32 session_final_guest_pc=cpuRegs.pc;
    const u64 session_final_ee_cycle=cpuRegs.cycle,session_final_iop_cycle=psxRegs.cycle;
    const bool session_final_has_booted_elf=VMManager::Internal::HasBootedELF();
    if(bios_only)CHECK(!session_final_has_booted_elf);
#ifdef PS2_SESSION_VERIFICATION
    if(bios_only)CHECK(session_final_ee_cycle>initial_ee_cycle&&session_final_iop_cycle>initial_iop_cycle);
#endif
    {std::lock_guard<std::mutex> lock(controls_mutex);terminal_snapshot=session.Snapshot();for(const auto id:published_ids)terminal_receipts.push_back(session.Inspect(id));terminal_available=true;controls=nullptr;}
    ps2_native_pad_set_input_owned(0);ps2_app_menu_publish(nullptr);
#ifdef PS2_SESSION_VERIFICATION
    {Ps2SessionStatus final{};CHECK(!published_ids.empty()&&ps2_session_status(published_ids.back(),&final)&&final.state==static_cast<unsigned>(ps2::SessionState::Stopped)&&final.outcome==static_cast<unsigned>(ps2::Outcome::Succeeded));}
    MTGS::WaitGS();CHECK(!ps2_hbxl_vm_av_model_capture_final());
#endif
#ifdef PS2_SESSION_VERIFICATION
    FILE*session_file=std::fopen("ps2-session.json","wb");CHECK(session_file);
    if(!verification_ok){
        std::fprintf(session_file,"{\"passed\":false,\"frames\":%llu,\"framesAfterEntry\":%llu,\"targetFrames\":%u,\"avReady\":%s,\"pausedVerified\":%s,\"saves\":%u,\"loads\":%u,\"checkpointRestoredExact\":%s,\"eeAdvancedAfterLoad\":%s,\"commandsAcknowledged\":%u,\"verificationMode\":true,\"nativeExecuted\":false}\n",
            (unsigned long long)ps2_session_presented_frames(),(unsigned long long)verified_frames,target,
            av_ready?"true":"false",paused_verified?"true":"false",saves,loads,
            restored_exact?"true":"false",advanced_after_load?"true":"false",commands);
    }else if(bios_only){
        std::fprintf(session_file,"{\"passed\":true,\"biosOnly\":true,\"frameEpoch\":\"bios-vm-start\",\"frames\":%llu,\"frameBaselineAtSessionStart\":%llu,\"framesAfterSessionStart\":%llu,\"targetFramesAfterSessionStart\":%u,\"frameTargetsRelativeToEntry\":false,\"pausedVerified\":true,\"resumes\":%u,\"saves\":%u,\"loads\":%u,\"checkpointRestoredExact\":true,\"eeAdvancedAfterLoad\":true,\"commandsAcknowledged\":%u,\"checkpointName\":\"%s\",\"autosaveName\":\"%s\",\"autosaveNamespace\":\"bios\",\"biosChecksum\":%u,\"automaticRestoreAttempted\":false,\"continuousDefault\":true,\"verificationMode\":true,\"nativeExecuted\":false}\n",
            (unsigned long long)ps2_session_presented_frames(),(unsigned long long)frame_baseline,(unsigned long long)verified_frames,target,resumes,saves,loads,commands,autosave_paths.backup_name.c_str(),autosave_paths.current_name.c_str(),BiosChecksum);
    }else{
        std::fprintf(session_file,"{\"passed\":true,\"frameEpoch\":\"elf-entry\",\"frames\":%llu,\"frameBaselineAfterEntry\":%llu,\"framesAfterEntry\":%llu,\"targetFramesAfterEntry\":%u,\"frameTargetsRelativeToEntry\":true,\"pausedVerified\":true,\"resumes\":%u,\"saves\":%u,\"loads\":%u,\"checkpointRestoredExact\":true,\"eeAdvancedAfterLoad\":true,\"commandsAcknowledged\":%u,\"continuousDefault\":true,\"verificationMode\":true,\"nativeExecuted\":false}\n",(unsigned long long)ps2_session_presented_frames(),(unsigned long long)frame_baseline,(unsigned long long)verified_frames,target,resumes,saves,loads,commands);
    }
    CHECK(!std::fclose(session_file));
#endif

    if(VMManager::GetState()!=VMState::Stopping)VMManager::SetState(VMState::Paused);
    if(card_experiment){
        Pad::ResetAllControllerInputs();FileMcd_Flush();
        bool save_error=false;const std::string checkpoint=EmuFolders::DataRoot+"/checkpoint.p2s";
        VMManager::SaveState(checkpoint.c_str(),false,false,[&](const std::string& message){std::fprintf(stderr,"State save: %s\n",message.c_str());save_error=true;});
        CHECK(!save_error && FileSystem::FileExists(checkpoint.c_str()));
    }
    const auto av=shutdown_vm();
    if(quick_exit_requested())record_quick_exit(false);
#ifdef PS2_SESSION_VERIFICATION
    CHECK(av.presented && av.audio_appends);
#else
    (void)av_ready;
#endif

#ifdef PS2_SESSION_VERIFICATION
    if(!verification_ok&&!quick_exit_requested()){ps2_hbxl_vm_stage("main-verification-failed-clean-shutdown");return 1;}
#endif
    std::printf("{\"passed\":true,\"consoleExecuted\":false,\"biosBooted\":%s,\"ps2ElfEntryReached\":%s,\"gameElfEntryReached\":%s,\"initialHasBootedELF\":%s,\"explicitResumeRequested\":%s,\"entryTransitionObserved\":%s,\"guestElfStateAvailable\":%s,\"eeJit\":true,\"iopJit\":true,\"entryAnchorBytes\":%u,\"entry\":%u,\"pc\":%u,\"eeCycles\":%llu,\"iopCycles\":%llu,\"batches\":%u,\"graphicsTested\":%s,\"gameplayTested\":false,\"biosOnly\":%s,\"fastBoot\":%s,\"discSource\":\"%s\",\"initialGuestPc\":%u,\"initialEeCycle\":%llu,\"initialIopCycle\":%llu,\"sessionFinalGuestPc\":%u,\"sessionFinalEeCycle\":%llu,\"sessionFinalIopCycle\":%llu,\"sessionFinalHasBootedELF\":%s,\"biosFirmwareExecuted\":%s,\"eeCycleFieldScope\":\"accumulated-executed-cycles\",\"pcAndIopCycleFieldScope\":\"before-session-loop\"}\n",
        bios_boot_observed?"true":"false",elf_entry_transition_observed?"true":"false",
#ifdef PS2_SPRITE_ORACLE
        "false",
#else
        elf_entry_transition_observed?"true":"false",
#endif
        initial_has_booted_elf?"true":"false",explicit_resume?"true":"false",
        elf_entry_transition_observed?"true":"false",guest_elf_state_available?"true":"false",
        config->anchor[0]?256u:0u,entry,final_pc,(unsigned long long)consumed,(unsigned long long)iop_cycles,batches,
#ifdef PS2_CAPTURE_SW_FRAME
        "true"
#else
        "false"
#endif
        ,bios_only?"true":"false",bios_only?"false":"true",bios_only?"NoDisc":"Auto",
        initial_guest_pc,(unsigned long long)initial_ee_cycle,(unsigned long long)initial_iop_cycle,
        session_final_guest_pc,(unsigned long long)session_final_ee_cycle,(unsigned long long)session_final_iop_cycle,
        session_final_has_booted_elf?"true":"false",
        bios_only&&session_final_ee_cycle>initial_ee_cycle&&av.presented?"true":"false"
    );ps2_hbxl_vm_stage("main-pass");return 0;
}
