// Registry of differential tests, one per reconstructed function.
#pragma once

#include <cstddef>

#include "../host/difftest.h"

namespace tests {

const protodroid::DiffTest* All(size_t* count);

void TestReport(protodroid::DiffResult* r);
void TestTraceVector(protodroid::DiffResult* r);
void TestNameHash(protodroid::DiffResult* r);
void TestCementLookup(protodroid::DiffResult* r);
void TestCementFileRange(protodroid::DiffResult* r);
void TestCementState(protodroid::DiffResult* r);
void TestMemory(protodroid::DiffResult* r);
void TestCementLoad(protodroid::DiffResult* r);
void TestCementLifetime(protodroid::DiffResult* r);
void TestFile(protodroid::DiffResult* r);

}  // namespace tests
