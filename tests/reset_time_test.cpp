#include "../src/reset_time.h"
#include <assert.h>
#include <string>

int main() {
  const int64_t epoch = parseResetTimestamp("2026-10-17T12:00:00Z");
  assert(epoch == 1792238400);
  assert(parseResetTimestamp("2026-10-17T14:00:00+02:00") == epoch);
  assert(parseResetTimestamp("2026-10-17T07:00:00-05:00") == epoch);
  assert(parseResetTimestamp("2026-10-17T12:00:00.123456Z") == epoch);
  assert(parseResetTimestamp("2024-02-29T00:00:00Z") > 0);
  assert(parseResetTimestamp("2025-02-29T00:00:00Z") == -1);
  assert(parseResetTimestamp("2026-10-17T24:00:00Z") == -1);
  assert(parseResetTimestamp("2026-10-17T12:00:00") == -1);
  assert(parseResetTimestamp("2026-10-17T12:00:00+99:00") == -1);
  assert(parseResetTimestamp("2026-10-17T12:00:00.Z") == -1);
  assert(parseResetTimestamp("") == -1);
  char out[24];
  formatResetRemaining(epoch + 2 * 86400 + 3 * 3600 + 4 * 60, epoch, out, sizeof(out));
  assert(std::string(out) == "2 03:04");
  formatResetRemaining(epoch + 1, epoch, out, sizeof(out));
  assert(std::string(out) == "0 00:01");
  formatResetRemaining(epoch - 1, epoch, out, sizeof(out));
  assert(std::string(out) == "0 00:00");
  formatResetRemaining(epoch, 0, out, sizeof(out));
  assert(std::string(out) == "-- --:--");
  formatResetRemaining(-1, epoch, out, sizeof(out));
  assert(std::string(out) == "-- --:--");
}
