#include <windows.h>
#include <dbgeng.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

int wmain(int argc, wchar_t **argv)
{
	if (argc != 3)
		return 2;
	DWORD pid = wcstoul(argv[1], nullptr, 10);
	if (!pid || _wcsicmp(fs::path(argv[2]).filename().c_str(), L"obs64.exe"))
		return 2;
	HANDLE target = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	wchar_t actual[MAX_PATH * 4]{};
	DWORD size = _countof(actual);
	bool matches = target && QueryFullProcessImageNameW(target, 0, actual, &size) &&
		       !_wcsicmp(actual, fs::absolute(argv[2]).lexically_normal().c_str());
	if (target)
		CloseHandle(target);
	if (!matches)
		return 2;

	ComPtr<IDebugClient> client;
	ComPtr<IDebugControl> control;
	ComPtr<IDebugSymbols3> symbols;
	ComPtr<IDebugSystemObjects> threads;
	if (FAILED(DebugCreate(__uuidof(IDebugClient), &client)) ||
	    FAILED(client.As(&control)) || FAILED(client.As(&symbols)) || FAILED(client.As(&threads)))
		return 3;

	fs::path root = fs::path(actual).parent_path();
	std::wstring path = root.wstring();
	if (fs::is_directory(root / L"core")) {
		for (auto &entry : fs::directory_iterator(root / L"core"))
			if (entry.is_directory())
				path += L";" + entry.path().wstring();
	}
	if (FAILED(symbols->SetSymbolPathWide(path.c_str())))
		return 4;

	// CEF starts several copies of this executable. The caller selects the recording server PID.
	auto hr = client->AttachProcess(0, pid, DEBUG_ATTACH_NONINVASIVE);
	if (FAILED(hr) || FAILED(hr = control->WaitForEvent(0, 10000))) {
		std::printf("Stack capture failed (0x%08lx)\n", static_cast<unsigned long>(hr));
		client->EndSession(DEBUG_END_ACTIVE_DETACH);
		return 5;
	}
	ULONG count = 0;
	threads->GetNumberThreads(&count);
	std::vector<ULONG> ids(count), systemIds(count);
	if (!count || FAILED(threads->GetThreadIdsByIndex(0, count, ids.data(), systemIds.data()))) {
		client->DetachProcesses();
		return 6;
	}
	std::printf("OSN recording server %lu: %lu threads\n", pid, count);
	for (ULONG i = 0; i < count; ++i) {
		if (FAILED(threads->SetCurrentThreadId(ids[i])))
			continue;
		DEBUG_STACK_FRAME frames[32]{};
		ULONG read = 0;
		if (FAILED(control->GetStackTrace(0, 0, 0, frames, _countof(frames), &read)))
			continue;
		std::printf("Thread %lu:", systemIds[i]);
		for (ULONG f = 0; f < read; ++f) {
			ULONG64 ip = frames[f].InstructionOffset, base = 0;
			ULONG module = 0;
			if (FAILED(symbols->GetModuleByOffset(ip, 0, &module, &base)))
				continue;
			char image[128]{};
			if (FAILED(symbols->GetModuleNameString(DEBUG_MODNAME_MODULE, module, base, image, sizeof(image), nullptr)))
				continue;
			char name[512]{};
			ULONG64 displacement = 0;
			std::printf(" %s+0x%llx", image, ip - base);
			if (symbols->GetNameByOffset(ip, name, sizeof(name), nullptr, &displacement) == S_OK) {
				const char *function = strchr(name, '!');
				if (function)
					std::printf("(%s+0x%llx)", function + 1, displacement);
			}
		}
		std::puts("");
	}
	client->DetachProcesses();
	return 0;
}
