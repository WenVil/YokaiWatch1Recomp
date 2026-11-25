// Windows dev compatibility shim (build/lasrecomp.exe -> sibling build/13SentinelsRecomp.exe).
// Keeps existing unmodified test scripts (e.g. work/run_regression_suite.ps1 invoking lasrecomp.exe) working
// while routing all imports to 13SentinelsRecomp.exe and cleaning up child processes via a Windows Job Object.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static void append_quoted_arg(wchar_t *buf, size_t cap, const wchar_t *arg) {
    size_t len = wcslen(buf);
    BOOL need_q = (!arg[0] || wcspbrk(arg, L" \t\"") != NULL);
    if (len && len + 1 < cap) buf[len++] = L' ';
    if (need_q && len + 1 < cap) buf[len++] = L'"';
    for (const wchar_t *p = arg; *p; p++) {
        int bs = 0;
        while (*p == L'\\') { bs++; p++; }
        int reps = !*p ? (need_q ? 2 * bs : bs) : (*p == L'"' ? 2 * bs + 1 : bs);
        for (int i = 0; i < reps && len + 1 < cap; i++) buf[len++] = L'\\';
        if (!*p) break;
        if (len + 1 < cap) buf[len++] = *p;
    }
    if (need_q && len + 1 < cap) buf[len++] = L'"';
    buf[len] = 0;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return 127;
    wchar_t *slash = wcsrchr(exe_dir, L'\\');
    if (!slash) return 127;
    slash[1] = 0;

    wchar_t target[MAX_PATH];
    if (_snwprintf(target, MAX_PATH, L"%s13SentinelsRecomp.exe", exe_dir) < 0) return 127;

    static wchar_t cmd[32768];
    cmd[0] = 0;
    append_quoted_arg(cmd, 32768, target);
    for (int i = 1; i < argc; i++) append_quoted_arg(cmd, 32768, argv[i]);

    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {0};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info);
    }

    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(target, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"[compat] failed to start %ls (error %lu)\n", target, GetLastError());
        if (job) CloseHandle(job);
        return 127;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = 1;
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    return (int)code;
}
