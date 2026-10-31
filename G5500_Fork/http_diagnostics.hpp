// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
struct HttpDiagnostics {
  uint32_t accepted = 0, readFailures = 0, rejected = 0, writeFailures = 0;
  uint32_t requests = 0;
  int writeErrno = 0;
  uint32_t writeBytes = 0, writeSize = 0;
  const char *writeCause = "none";
  uint32_t htmlFailures = 0, htmlBytes = 0, htmlSize = 0;
  int htmlErrno = 0;
  const char *htmlCause = "none";
};
} // namespace forkrot
