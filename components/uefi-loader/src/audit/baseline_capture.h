#ifndef IB_AUDIT_BASELINE_CAPTURE_H
#define IB_AUDIT_BASELINE_CAPTURE_H

#include <stddef.h>
#include <wchar.h>

#include "baseline.h"

int ib_audit_windows_capture_baseline(const char *log_path,
                                      const wchar_t *esp_root,
                                      ib_audit_baseline_t *state,
                                      wchar_t *error, size_t error_count);

#endif
