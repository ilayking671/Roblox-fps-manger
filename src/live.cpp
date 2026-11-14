#include "live.h"
#include <evntcons.h>
#include <algorithm>
#include <cstring>

namespace {

constexpr wchar_t kSession[] = L"RobloxFpsManager.Live";
constexpr GUID kDxgi = {0xca11c036, 0x0102, 0x4a2d, {0xa6, 0xad, 0xf0, 0x3c, 0xfe, 0xd5, 0xd3, 0xc9}};
constexpr USHORT kPresentStart = 42;
constexpr USHORT kPresentMpoStart = 55;

}

LiveMonitor::~LiveMonitor()
{
    Stop();
}

std::vector<BYTE> LiveMonitor::Properties() const
{
    size_t nameBytes = (wcslen(kSession) + 1) * sizeof(wchar_t);
    std::vector<BYTE> buf(sizeof(EVENT_TRACE_PROPERTIES) + nameBytes, 0);
    auto* p = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buf.data());
    p->Wnode.BufferSize = static_cast<ULONG>(buf.size());
    p->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    p->Wnode.ClientContext = 1;
    p->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    p->BufferSize = 64;
    p->MinimumBuffers = 4;
    p->MaximumBuffers = 32;
    p->FlushTimer = 1;
    p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    return buf;
}

bool LiveMonitor::Start()
{
    if (status_ == Status::Running) return true;
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    msPerTick_ = 1000.0 / static_cast<double>(f.QuadPart);

    auto props = Properties();
    ULONG r = StartTraceW(&session_, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()));
    if (r == ERROR_ALREADY_EXISTS) {
        auto stop = Properties();
        ControlTraceW(0, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(stop.data()), EVENT_TRACE_CONTROL_STOP);
        props = Properties();
        r = StartTraceW(&session_, kSession, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()));
    }
    if (r != ERROR_SUCCESS) {
        session_ = 0;
        status_ = r == ERROR_ACCESS_DENIED ? Status::Denied : Status::Failed;
        return false;
    }
    r = EnableTraceEx2(session_, &kDxgi, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_INFORMATION,
                       0xFFFFFFFFFFFFFFFFull, 0, 0, nullptr);
    if (r != ERROR_SUCCESS) {
        Stop();
        status_ = r == ERROR_ACCESS_DENIED ? Status::Denied : Status::Failed;
        return false;
    }

    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName = const_cast<LPWSTR>(kSession);
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = OnEvent;
    log.Context = this;
    trace_ = OpenTraceW(&log);
    if (trace_ == INVALID_PROCESSTRACE_HANDLE) {
        Stop();
        status_ = Status::Failed;
        return false;
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        last_.clear();
        pending_.clear();
    }
    thread_ = CreateThread(nullptr, 0, Pump, this, 0, nullptr);
    if (!thread_) {
        Stop();
        status_ = Status::Failed;
        return false;
    }
    status_ = Status::Running;
    return true;
}

void LiveMonitor::Stop()
{
    if (session_) {
        auto props = Properties();
        ControlTraceW(session_, nullptr, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()), EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
    }
    if (trace_ != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace_);
        trace_ = INVALID_PROCESSTRACE_HANDLE;
    }
    if (thread_) {
        WaitForSingleObject(thread_, 3000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (status_ == Status::Running) status_ = Status::Off;
}

void LiveMonitor::Flush()
{
    if (!session_) return;
    auto props = Properties();
    ControlTraceW(session_, nullptr, reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props.data()), EVENT_TRACE_CONTROL_FLUSH);
}

void LiveMonitor::SetPids(const std::vector<DWORD>& pids)
{
    std::lock_guard<std::mutex> g(lock_);
    pids_ = pids;
    for (auto it = last_.begin(); it != last_.end();) {
        it = std::find(pids_.begin(), pids_.end(), it->first) == pids_.end() ? last_.erase(it) : std::next(it);
    }
}

void LiveMonitor::Collect(std::unordered_map<DWORD, std::vector<float>>& out)
{
    out.clear();
    std::lock_guard<std::mutex> g(lock_);
    out.swap(pending_);
}

void WINAPI LiveMonitor::OnEvent(PEVENT_RECORD rec)
{
    if (rec && rec->UserContext) static_cast<LiveMonitor*>(rec->UserContext)->Handle(rec);
}

void LiveMonitor::Handle(PEVENT_RECORD rec)
{
    const auto& h = rec->EventHeader;
    if (!IsEqualGUID(h.ProviderId, kDxgi)) return;
    USHORT id = h.EventDescriptor.Id;
    if (id != kPresentStart && id != kPresentMpoStart) return;
    DWORD pid = h.ProcessId;
    LONGLONG ts = h.TimeStamp.QuadPart;
    std::lock_guard<std::mutex> g(lock_);
    if (std::find(pids_.begin(), pids_.end(), pid) == pids_.end()) return;
    auto it = last_.find(pid);
    if (it != last_.end()) {
        double ms = static_cast<double>(ts - it->second) * msPerTick_;
        if (ms > 0.05 && ms < 5000.0) {
            auto& v = pending_[pid];
            if (v.size() < 20000) v.push_back(static_cast<float>(ms));
        }
        it->second = ts;
    } else {
        last_[pid] = ts;
    }
}

DWORD WINAPI LiveMonitor::Pump(LPVOID p)
{
    auto* self = static_cast<LiveMonitor*>(p);
    TRACEHANDLE t = self->trace_;
    ProcessTrace(&t, 1, nullptr, nullptr);
    return 0;
}
