/**
 * @file src/platform/windows/nvprefs/driver_settings.h
 * @brief Declarations for nvidia driver settings.
 */
#pragma once

// nvapi headers
// disable clang-format header reordering
// as <NvApiDriverSettings.h> needs types from <nvapi.h>
// clang-format off
#ifdef __MINGW32__
#ifdef __success
#undef __success
#endif
#define __success(expr)
#ifndef __in
#define __in
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __in_bcount
#define __in_bcount(size)
#endif
#ifndef __in_ecount
#define __in_ecount(size)
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __inout_ecount_full
#define __inout_ecount_full(size)
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(size, length)
#endif
#ifndef __out
#define __out
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(size)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(size)
#endif
#endif
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#ifdef __MINGW32__
#undef __success
#undef __in
#undef __in_opt
#undef __in_bcount
#undef __in_ecount
#undef __inout
#undef __inout_opt
#undef __inout_ecount_full
#undef __inout_ecount_part_opt
#undef __out
#undef __out_opt
#undef __out_ecount_full_opt
#undef __out_ecount_opt
#endif
// clang-format on

// local includes
#include "undo_data.h"

namespace nvprefs {

  class driver_settings_t {
  public:
    ~driver_settings_t();

    bool init();

    void destroy();

    bool load_settings();

    bool save_settings();

    bool restore_global_profile_to_undo(const undo_data_t &undo_data);

    bool check_and_modify_global_profile(std::optional<undo_data_t> &undo_data);

    bool check_and_modify_application_profile(bool &modified);

  private:
    NvDRSSessionHandle session_handle = nullptr;
  };

}  // namespace nvprefs
