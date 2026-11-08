// Deterministic host harness for the per-target detour chain. The fake backend models Dobby's original trampoline;
// it does not claim to validate ARM64 code patching on a device.
#include "../mod_hook.cpp"

#include <cstdlib>
#include <iostream>
#include <unordered_map>

namespace {
std::unordered_map<uint32_t, RecompFn> targets;
std::unordered_map<void*, void*> replacements;
bool fail_next = false;
int checks = 0;
std::string sequence;

void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void Invoke(RecompFn target, Ctx* c) {
    auto found = replacements.find(reinterpret_cast<void*>(target));
    if (found == replacements.end()) target(c);
    else reinterpret_cast<RecompFn>(found->second)(c);
}
}  // namespace

extern "C" RecompFn RecompLookup(uint32_t target) {
    auto found = targets.find(target);
    return found == targets.end() ? nullptr : found->second;
}

extern "C" int DobbyHook(void* address, void* fake, void** original) {
    if (fail_next) {
        fail_next = false;
        return 1;
    }
    *original = address;
    replacements[address] = fake;
    return 0;
}

namespace native {
void Log(const char*, ...) {}
}  // namespace

namespace {
native::ModHookFn original_a = nullptr;
void Original(Ctx*) { sequence += "1"; }
void OriginalTwo(Ctx*) { sequence += "2"; }
void OriginalThree(Ctx*) { sequence += "3"; }

void A(Ctx* c) {
    sequence += "A";
    original_a(c);
    sequence += "a";
}

native::ModHookFn original_b = nullptr;
void B(Ctx* c) {
    sequence += "B";
    original_b(c);
    sequence += "b";
}

native::ModHookFn original_c = nullptr;
void C(Ctx* c) {
    sequence += "C";
    original_c(c);
    sequence += "c";
}

native::ModHookFn original_d = nullptr;
void D(Ctx* c) {
    sequence += "D";
    original_d(c);
    sequence += "d";
}
}  // namespace

int main() {
    using namespace native;
    Ctx context{};
    targets[0x1000] = Original;
    Check(ModHookInstall(0x1000, A, &original_a) && original_a == Original, "first original is real target");
    Check(ModHookInstall(0x1000, B, &original_b) && original_b == A, "second original is first detour");
    Check(!ModHookInstall(0x1000, B, &original_b), "same detour cannot wrap itself");
    Check(ModHookInstall(0x1000, C, &original_c) && original_c == B, "third original is second detour");
    sequence.clear();
    Invoke(Original, &context);
    Check(sequence == "CBA1abc", "all detours and original run exactly once in chain order");

    targets[0x2000] = OriginalTwo;
    Check(ModHookInstall(0x2000, D, &original_d) && original_d == OriginalTwo, "independent target slot");
    sequence.clear();
    Invoke(OriginalTwo, &context);
    Check(sequence == "D2d", "independent target chain");
    sequence.clear();
    Invoke(Original, &context);
    Check(sequence == "CBA1abc", "first chain remains intact");

    ModHookFn missing = nullptr;
    Check(!ModHookInstall(0xdead, A, &missing), "unknown recompiled target fails");
    targets[0x3000] = OriginalThree;
    fail_next = true;
    Check(!ModHookInstall(0x3000, A, &missing), "backend failure is returned");
    Check(ModHookInstall(0x3000, A, &original_a) && original_a == OriginalThree, "failed slot can be retried");
    sequence.clear();
    Invoke(OriginalThree, &context);
    Check(sequence == "A3a", "retry installs a working chain");

    std::cout << "mod hook tests passed: " << checks << " checks\n";
    return 0;
}
