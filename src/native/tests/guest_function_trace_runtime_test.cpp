#include "guest.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
uint8_t* RecompMemoryBase = nullptr;
}

namespace native {
namespace {
std::vector<std::string> logs;
int trace_setting = 1;
}

void Log(const char* format, ...) {
    char buffer[2048];
    va_list arguments;
    va_start(arguments, format);
    const int result = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (result < 0 || static_cast<size_t>(result) >= sizeof(buffer)) std::abort();
    logs.emplace_back(buffer);
}

int SettingInt(const char* key, int) {
    return std::string(key) == "guest_function_trace" ? trace_setting : 0;
}

}  // namespace native

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "disabled") {
        native::trace_setting = 0;
        RecompGuestFunctionEnter(nullptr, 0x004e5250);
        RecompGuestFunctionExit(nullptr, 0x004e5250);
        Check(native::logs.empty(), "disabled trace read guest state or emitted a record");
        std::cerr << "disabled guest function trace test passed\n";
        return 0;
    }
    Check(argc == 1, "unknown test mode");
    static uint8_t memory[4096];
    RecompMemoryBase = memory;
    Ctx context{};
    context.esp = 0x100;
    context.ecx = 0x11223344;
    SW32(context.esp, 0x10203040);
    SW32(context.esp + 4, 2);
    SW32(context.esp + 8, 0);
    SW32(context.esp + 12, 0x55667788);

    RecompGuestFunctionEnter(&context, 0x004e5250);
    RecompGuestFunctionEnter(&context, 0x00838b40);
    RecompGuestFunctionExit(&context, 0x00838b40);
    Check(native::logs.empty(), "inner completion logged inside its measured parent");
    RecompGuestFunctionExit(&context, 0x004e5250);
    Check(native::logs.size() == 2, "outermost exit did not flush both records");
    Check(native::logs[0].find("event=complete sequence=1 rva=00838b40") != std::string::npos,
          "inner record or sequence differs");
    Check(native::logs[1].find("event=complete sequence=2 rva=004e5250") != std::string::npos,
          "outer record or sequence differs");
    Check(native::logs[0].find("ecx=11223344 caller=10203040 arg0=00000002 arg1=00000000 arg2=55667788") !=
              std::string::npos,
          "raw entry metadata differs");

    for (const std::string& line : native::logs) std::cout << line << "\n";
    std::cerr << "guest function trace runtime tests passed\n";
    return 0;
}
