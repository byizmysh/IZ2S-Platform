"""Keep screenshot workers joinable until their owner reaps them."""

from hashlib import sha256
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


work = Path(__file__).resolve().parent
project = Path('${IZPS2_WORKSPACE}')
series = project.resolve() / 'IZ-R799/run25/jit-single-range'
source = Path('${PS2_NATIVE_SOURCE}/vendor/ARMSX2/pcsx2/GS/Renderers/Common/GSRenderer.cpp')
report_source = source.parents[2] / 'DriverReport/GSDriverReport.cpp'
out = work / 'nfs-snapshot-worker'


def substitute(code, old, new):
    if code.count(old) != 1:
        raise ValueError(f'Bad screenshot source anchor {old!r}')
    return code.replace(old, new)


def own_snapshot_worker(code):
    code = substitute(code, '#include <deque>', '#include <list>')
    code = substitute(code, 'static std::deque<std::thread> s_screenshot_threads;', '''struct GSScreenshotWorker
{
	explicit GSScreenshotWorker(auto&& write) : thread(std::forward<decltype(write)>(write)) {}
	std::thread thread;
	bool finished = false;
};

static std::list<GSScreenshotWorker> s_screenshot_threads;''')
    marker = '// `announce` false is a screenshot that goes into a dump\'s zip:'
    code = substitute(code, marker, '''static void ReapScreenshotWorkers(bool all)
{
	std::list<GSScreenshotWorker> joinable;
	{
		const std::lock_guard lock(s_screenshot_threads_mutex);
		if (all)
		{
			joinable.splice(joinable.end(), s_screenshot_threads);
		}
		else
		{
			for (auto it = s_screenshot_threads.begin(); it != s_screenshot_threads.end();)
			{
				const auto worker = it++;
				if (worker->finished)
					joinable.splice(joinable.end(), s_screenshot_threads, worker);
			}
		}
	}
	for (auto& worker : joinable)
		worker.thread.join();
}

''' + marker)
    code = substitute(code, '''	// maybe std::async would be better here.. but it's definitely worth threading, large screenshots take a while to compress.
	std::unique_lock lock(s_screenshot_threads_mutex);''', '''	ReapScreenshotWorkers(false);
	const std::lock_guard lock(s_screenshot_threads_mutex);''')
    code = substitute(code, '''		// remove ourselves from the list, if the GS thread is waiting for us, we won't be in there
		const auto this_id = std::this_thread::get_id();
		std::unique_lock lock(s_screenshot_threads_mutex);
		for (auto it = s_screenshot_threads.begin(); it != s_screenshot_threads.end(); ++it)
		{
			if (it->get_id() == this_id)
			{
				it->detach();
				s_screenshot_threads.erase(it);
				break;
			}
		}''', '''		// If shutdown already took the node, its owner will join this thread.
		const auto this_id = std::this_thread::get_id();
		const std::lock_guard lock(s_screenshot_threads_mutex);
		for (auto& worker : s_screenshot_threads)
		{
			if (worker.thread.get_id() == this_id)
			{
				worker.finished = true;
				break;
			}
		}''')
    return substitute(code, '''void GSJoinSnapshotThreads()
{
	std::unique_lock lock(s_screenshot_threads_mutex);
	while (!s_screenshot_threads.empty())
	{
		std::thread save_thread(std::move(s_screenshot_threads.front()));
		s_screenshot_threads.pop_front();
		lock.unlock();
		save_thread.join();
		lock.lock();
	}
}''', '''void GSJoinSnapshotThreads()
{
	ReapScreenshotWorkers(true);
}''')


def use_hosted_inline_report(code):
    code = substitute(code, '\t\t/// Runs RunSystemPart on a detached thread.',
        '#if !defined(__SWITCH__) && !defined(PS2_HOST_CODE_BRIDGE)\n\t\t/// Runs RunSystemPart on a detached thread.')
    code = substitute(code, '\n\t\tvoid WriteSettings(JsonWriter& w)', '\n#endif\n\n\t\tvoid WriteSettings(JsonWriter& w)')
    code = substitute(code, '\t\tif (StartDetached(part))', '''#if defined(__SWITCH__) || defined(PS2_HOST_CODE_BRIDGE)
		// A hosted worker's code may unload on return. Use the existing inline
		// path without creating a detached system-report helper.
		steps.Add("system_part.thread", true, "ran inline on the hosted worker", 0.0);
		RunSystemPart(part);
#else
		if (StartDetached(part))''')
    return substitute(code, '''			RunSystemPart(part);
		}

		std::unique_lock lock(part->mutex);''', '''			RunSystemPart(part);
		}
#endif

		std::unique_lock lock(part->mutex);''')


def compile_command(cmd, file, obj):
    result = cmd.copy()
    for flag, path in (('-c', file), ('-o', obj), ('-MF', obj.with_suffix('.d')), ('-MT', obj)):
        if flag in result:
            result[result.index(flag) + 1] = str(path)
    return result


def run(cmd, label, cwd=out):
    with (out / label).open('wb') as log:
        subprocess.run(cmd, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)


def build_local():
    out.mkdir(exist_ok=True)
    baseline = project / 'build/portable-gs-bti-v1'
    report = json.loads((baseline / 'report.json').read_text())
    raw = source.read_bytes()
    assert sha256(raw).hexdigest() == report['inputs'][str(source)]
    file = out / 'GSRenderer.cpp'
    file.write_text(own_snapshot_worker(raw.decode()))
    saved, = [argv for argv in report['commands'] if str(source) in argv]
    object_file = out / 'GSRenderer.cpp.o'
    command = compile_command(saved, file, object_file)
    command.extend(('-iquote', str(source.parent)))
    run(command, 'local-compile.log', baseline)
    baseline_archive = work / 'nfs-extended-build/wait-experiment-v2/libpcsx2-validation.a'
    old_member = subprocess.check_output(['llvm-ar', 'p', str(baseline_archive), 'GSRenderer.cpp.o'])
    assert sha256(old_member).hexdigest() == '75191c14f39c8c1df39cdb1d2bcd2c55c01ff30ad329a43ac66ad0eccd2bf978'
    archive = out / baseline_archive.name
    shutil.copyfile(baseline_archive, archive)
    run(['llvm-ar', 'rD', str(archive), str(object_file)], 'local-archive.log')
    link = json.loads((baseline_archive.parent / 'recipe.json').read_text())[-1]
    link[link.index(str(baseline_archive))] = str(archive)
    link[link.index('-o') + 1] = str(out / 'nfs-snapshot-arm64')
    run(link, 'local-link.log')
    (out / 'local-recipes.json').write_text(json.dumps({'compile': command, 'link': link}, indent=2) + '\n')


def build_local_report():
    fixture = Path('${PS2_NATIVE_SOURCE}')
    model = fixture / 'build/armsx2-arm64-cpu'
    archive_path = out / 'libpcsx2-validation.a'
    object_name = 'GSDriverReport.cpp.o'
    original = model / 'pcsx2/CMakeFiles/PCSX2.dir/GS/DriverReport' / object_name
    member = subprocess.check_output(['llvm-ar', 'p', str(archive_path), object_name])
    assert member == original.read_bytes()
    assert sha256(member).hexdigest() == '2ff7e242894afcd853db5e584a8587a586f369c41d02eba99eee741ac5cc6c44'
    raw_source = report_source.read_bytes()
    assert sha256(raw_source).hexdigest() == 'f591c5f3504234315580f98f8ebcef7336203c8fff1290df0744b576a043bbda'
    patched = out / report_source.name
    patched.write_text(use_hosted_inline_report(raw_source.decode()))
    saved, = [entry for entry in json.loads((model / 'compile_commands.json').read_text())
              if Path(entry['file']).name == report_source.name]
    command = [arg.replace('${IZPS2_MODEL_TOOLS}', '${IZPS2_DEPENDENCIES}') for arg in shlex.split(saved['command'])]
    object_file = out / object_name
    command = compile_command(command, patched, object_file)
    command.insert(0, str(fixture / 'tools/cross_header_launcher.py'))
    command.extend(('-ivfsoverlay', str(project / 'build/portable-gs-bti-v1/headers.vfs.json'), '-iquote', str(report_source.parent)))
    assert '-DPS2_HOST_CODE_BRIDGE=1' in command
    run(command, 'local-report-compile.log', model)
    archive = out / 'report-joined' / archive_path.name
    archive.parent.mkdir(exist_ok=True)
    shutil.copyfile(archive_path, archive)
    run(['llvm-ar', 'rD', str(archive), str(object_file)], 'local-report-archive.log')
    link = json.loads((out / 'local-recipes.json').read_text())['link']
    link[link.index(str(archive_path))] = str(archive)
    link[link.index('-o') + 1] = str(out / 'nfs-snapshot-report-arm64')
    run(link, 'local-report-link.log')
    (out / 'local-report-recipes.json').write_text(json.dumps({'compile': command, 'link': link}, indent=2) + '\n')


if __name__ == '__main__':
    if sys.argv[1:] == ['--report']:
        build_local_report()
    else:
        build_local()
