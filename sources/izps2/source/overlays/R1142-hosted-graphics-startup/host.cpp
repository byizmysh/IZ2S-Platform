// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Minimal native host. Desktop clipboard, web, text entry and account UI are unavailable.
// CPU/GS scheduling, errors, controls and cooperative lifecycle are implemented below.

#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/GameList.h"
#include "pcsx2/Host.h"
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/IopThread/OwnerAsserts.h"
#include "host.h"
#include "ps2_vm_runner.h"
#include "MTGS.h"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include "pcsx2/VMManager.h"
#include "common/ProgressCallback.h"

#include <mutex>
#include <utility>
#include <vector>

void Host::CommitBaseSettingChanges()
{
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
	return ProgressCallback::CreateNullProgressCallback();
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	std::fprintf(stderr, "%.*s: %.*s\n", int(title.size()), title.data(), int(message.size()), message.data());
}

static void save_error(std::string_view title, std::string_view message);
void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
    save_error(title, message);
}

void Host::OpenURL(const std::string_view url)
{
}

bool Host::InBatchMode()
{
	return false;
}

bool Host::InNoGUIMode()
{
	return true;
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
	return false;
}

std::string Host::GetTextFromClipboard()
{
	return std::string();
}

void Host::BeginTextInput()
{
}

void Host::EndTextInput()
{
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::SetMouseLock(bool state)
{
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool) {
    WindowInfo info; info.type = WindowInfo::Type::Surfaceless;
    info.surface_width = 960; info.surface_height = 540; info.surface_refresh_rate = 60.0f;
    return info;
}

void Host::ReleaseRenderWindow()
{
}

void Host::BeginPresentFrame()
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
	std::fprintf(stderr, "IZPS2_VM_STARTED\n");
}

void Host::OnVMDestroyed()
{
	std::fprintf(stderr, "IZPS2_VM_DESTROYED\n");
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
		const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

// Worker requests are marshalled to the CPU owner. Blocking requests signal only
// after their function has actually run; no detached or discarded core tasks.
namespace {
struct CpuTask { std::function<void()> function; bool done = false; std::mutex mutex; std::condition_variable ready; };
std::mutex task_mutex, error_mutex;
std::vector<std::shared_ptr<CpuTask>> tasks;
std::atomic<bool> stop_requested{false};
std::string last_error;
std::string startup_errors;
void (*nro_pump)() = nullptr;
}
static void save_error(std::string_view title, std::string_view message) {
    std::fprintf(stderr, "ERROR %.*s: %.*s\n", int(title.size()), title.data(), int(message.size()), message.data());
    std::lock_guard lock(error_mutex);
    last_error = std::string(title) + ": " + std::string(message);
    constexpr size_t max_startup_errors = 3072;
    if (startup_errors.size() + 1 < max_startup_errors) {
        startup_errors.append(last_error, 0, max_startup_errors - startup_errors.size() - 1);
        startup_errors.push_back('\n');
    }
}
std::string iz_take_startup_errors() {
    std::lock_guard lock(error_mutex);
    std::string errors;
    errors.swap(startup_errors);
    return errors;
}
void iz_host_set_nro_pump(void (*pump)()) { nro_pump = pump; }
void iz_request_stop() { stop_requested.store(true, std::memory_order_release); }
bool iz_stop_requested() { return stop_requested.load(std::memory_order_acquire); }
std::string iz_last_error() { std::lock_guard lock(error_mutex); return last_error; }
void Host::RunOnCPUThread(std::function<void()> function, bool block) {
    if (VMManager::Internal::IsOnCPUThread()) { function(); return; }
    auto task = std::make_shared<CpuTask>(); task->function = std::move(function);
    { std::lock_guard lock(task_mutex); tasks.push_back(task); }
    if (block) { std::unique_lock lock(task->mutex); task->ready.wait(lock, [&] { return task->done; }); }
}
void Host::RunOnGSThread(std::function<void()> function) { MTGS::RunOnGSThread(std::move(function)); }

void Host::RefreshGameListAsync(bool invalidate_cache)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
	return false;
}

void Host::SetFullscreen(bool enabled)
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
	iz_request_stop();
}

void Host::RequestExitBigPicture()
{
	iz_request_stop();
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	iz_request_stop();
}

void Host::PumpMessagesOnCPUThread() {
    std::vector<std::shared_ptr<CpuTask>> pending;
    { std::lock_guard lock(task_mutex); pending.swap(tasks); }
    for (const auto& task : pending) {
        task->function();
        { std::lock_guard lock(task->mutex); task->done = true; }
        task->ready.notify_one();
    }
    if (ps2_vm_pump_module(iz_stop_requested())) return;
    if (nro_pump) nro_pump();
}

s32 Host::Internal::GetTranslatedStringImpl(
	const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
	if (msg.size() > tbuf_space)
		return -1;
	else if (msg.empty())
		return 0;

	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
	TinyString count_str = TinyString::from_format("{}", count);

	std::string ret(msg);
	for (;;)
	{
		std::string::size_type pos = ret.find("%n");
		if (pos == std::string::npos)
			break;

		ret.replace(pos, 2, count_str.view());
	}

	return ret;
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
}

bool Host::HasNativeAchievementNotifications() { return false; }
void Host::OnAchievementNotification(const char*, float, const char*, const char*, const char*) {}

void Host::OnAchievementsRefreshed()
{
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
}

bool Host::LocaleCircleConfirm()
{
	return false;
}

bool Host::ShouldPreferHostFileSelector()
{
	return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
	FileSelectorFilters filters, std::string_view initial_directory)
{
	callback(std::string());
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
	int res = std::strncmp(lhs.data(), rhs.data(), std::min(lhs.size(), rhs.size()));
	if (res != 0)
		return res;
	return lhs.size() > rhs.size() ? 1 : lhs.size() < rhs.size() ? -1 : 0;
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
	return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
	return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
	return nullptr;
}

BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()
