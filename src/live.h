#pragma once
#include <windows.h>
#include <evntrace.h>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

class LiveMonitor {
public:
    enum class Status { Off, Running, Denied, Failed };

    ~LiveMonitor();
    bool Start();
    void Stop();
    void Flush();
    void SetPids(const std::vector<DWORD>& pids);
    void Collect(std::unordered_map<DWORD, std::vector<float>>& out);
    Status GetStatus() const { return status_; }
    bool Active() const { return status_ == Status::Running; }

private:
    static void WINAPI OnEvent(PEVENT_RECORD rec);
    static DWORD WINAPI Pump(LPVOID p);
    void Handle(PEVENT_RECORD rec);
    std::vector<BYTE> Properties() const;

    TRACEHANDLE session_ = 0;
    TRACEHANDLE trace_ = INVALID_PROCESSTRACE_HANDLE;
    HANDLE thread_ = nullptr;
    std::atomic<Status> status_{Status::Off};
    double msPerTick_ = 0;
    std::mutex lock_;
    std::vector<DWORD> pids_;
    std::unordered_map<DWORD, LONGLONG> last_;
    std::unordered_map<DWORD, std::vector<float>> pending_;
};
