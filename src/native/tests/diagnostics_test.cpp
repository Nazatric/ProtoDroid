#include "proc_stats.h"
#include "timing_window.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "diagnostics test failed: %s\n", message);
        std::exit(1);
    }
}

bool Near(double a, double b, double tolerance = 0.02) { return std::fabs(a - b) <= tolerance; }

void TestDistributionThresholdsAndWorstEvents() {
    native::TimingWindow window;
    int64_t now = 1000000000ll;
    for (int i = 0; i < 99; ++i) {
        now += 10000000ll;
        window.Add(now, 10000000ll);
    }
    now += 100000000ll;
    window.Add(now, 100000000ll);

    auto snapshot = window.Take();
    Check(snapshot.count == 100 && snapshot.total_ns == 1090000000ll, "window count or total is wrong");
    Check(Near(snapshot.ElapsedSeconds(), 1.09, 0.0001), "window elapsed time is wrong");
    Check(Near(snapshot.Frequency(), 100.0 / 1.09, 0.001), "window frequency is wrong");
    Check(Near(snapshot.AverageMs(), 10.9), "window average is wrong");
    Check(Near(snapshot.PercentileMs(50), 10.0), "p50 is wrong");
    Check(Near(snapshot.PercentileMs(95), 10.0), "p95 is wrong");
    Check(Near(snapshot.PercentileMs(99), 10.0), "nearest-rank p99 is wrong");
    Check(Near(snapshot.MaxMs(), 100.0), "maximum is wrong");
    Check(snapshot.over[0] == 100 && snapshot.over[1] == 1 && snapshot.over[2] == 1 && snapshot.over[3] == 1 &&
              snapshot.over[4] == 0,
          "fixed long-frame thresholds are wrong");
    Check(snapshot.worst[0].at_ns == now && snapshot.worst[0].duration_ns == 100000000ll,
          "worst event timestamp was not retained");
    Check(snapshot.worst[1].duration_ns == 10000000ll && snapshot.worst[3].duration_ns == 10000000ll,
          "worst event ordering is wrong");

    Check(window.Take().count == 0, "Take did not reset the window");
    now += 20000000ll;
    window.Add(now, 20000000ll);
    auto next = window.Take();
    Check(next.count == 1 && Near(next.ElapsedSeconds(), 0.02, 0.0001),
          "a new window did not continue from the preceding event");
}

void TestWorstEventTiesAndZeroDuration() {
    native::TimingWindow window;
    for (int i = 0; i < 6; ++i) window.Add(1000 + i, i == 5 ? 50 : 10);
    auto snapshot = window.Take();
    Check(snapshot.worst[0].duration_ns == 50 && snapshot.worst[0].at_ns == 1005, "new maximum was not inserted");
    Check(snapshot.worst[3].duration_ns == 10, "bounded worst-event list was not populated");

    native::TimingWindow zero;
    zero.Add(100, 0);
    Check(zero.Take().count == 1, "zero-duration limiter samples must be countable");
}

std::string ProcLine(const std::string& comm) {
    std::vector<std::string> fields(39, "0");
    fields[0] = "R";
    fields[11] = "1234";
    fields[12] = "567";
    fields[19] = "987654";
    fields[36] = "5";
    std::string line = "4242 (" + comm + ") ";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) line += ' ';
        line += fields[i];
    }
    return line;
}

void TestProcStatParser() {
    native::ProcStatInfo info;
    Check(native::ParseProcStat(ProcLine("dxvk shader ) worker"), info), "valid proc stat was rejected");
    Check(info.pid == 4242 && info.comm == "dxvk shader ) worker" && info.state == 'R', "pid/comm/state parsed incorrectly");
    Check(info.user_jiffies == 1234 && info.system_jiffies == 567 && info.start_time_jiffies == 987654 &&
              info.processor == 5,
          "CPU fields parsed at the wrong offsets");
    Check(!native::ParseProcStat("1 (short) R 0 0", info), "truncated proc stat was accepted");
}

}  // namespace

int main() {
    TestDistributionThresholdsAndWorstEvents();
    TestWorstEventTiesAndZeroDuration();
    TestProcStatParser();
    std::puts("diagnostics tests passed (timing windows, thresholds, worst events, /proc parsing)");
    return 0;
}
