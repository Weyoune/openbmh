// SPDX-License-Identifier: GPL-3.0-only
#include "launch/process.hpp"
#include "launch/archive.hpp"
#include <chrono>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
extern char **environ;
#endif
namespace bmh::launch {
int run_child(const std::filesystem::path &executable, const std::vector<std::string> &args,
              const std::function<void()> &pump) {
#ifdef _WIN32
    const auto quote = [](const std::wstring &value) {
        std::wstring out = L"\"";
        size_t slashes = 0;
        for (wchar_t c : value) {
            if (c == L'\\') {
                ++slashes;
                continue;
            }
            if (c == L'\"')
                out.append(slashes * 2 + 1, L'\\');
            else
                out.append(slashes, L'\\');
            out += c;
            slashes = 0;
        }
        out.append(slashes * 2, L'\\');
        out += L'\"';
        return out;
    };
    auto command = quote(executable.wstring());
    for (const auto &a : args) {
        command += L' ';
        command += quote(path_from_utf8(a).wstring());
    }
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        executable.parent_path().c_str(), &start, &process))
        throw std::runtime_error("Cannot start OpenBMH game process");
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT)
        if (pump)
            pump();
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return int(code);
#else
    const auto binary = path_to_utf8(executable);
    std::vector<std::string> storage{binary};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char *> argv;
    for (auto &s : storage)
        argv.push_back(s.data());
    argv.push_back(nullptr);
    pid_t pid;
    const int error = posix_spawn(&pid, binary.c_str(), nullptr, nullptr, argv.data(), environ);
    if (error)
        throw std::runtime_error("Cannot start OpenBMH game process (" + std::to_string(error) + ")");
    int status = 0;
    for (;;) {
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            break;
        if (result < 0 && errno != EINTR)
            throw std::runtime_error("Cannot wait for game process");
        if (pump)
            pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
}
} // namespace bmh::launch
