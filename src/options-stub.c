//
// Copyright (C) 2021 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdlib.h>

#include "mimalloc.h"
#include "mimalloc/internal.h"

int mi_version(void) mi_attr_noexcept {
  return MI_MALLOC_VERSION;
}

// --------------------------------------------------------
// Options
// These can be accessed by multiple threads and may be
// concurrently initialized, but an initializing data race
// is ok since they resolve to the same value.
// --------------------------------------------------------
typedef struct mi_option_desc_s {
  long        value;  // the value
  mi_option_t option; // for debugging: the option index should match the option
  const char* name;   // option name without `mimalloc_` prefix
  const char* legacy_name; // potential legacy option name
} mi_option_desc_t;

#define MI_OPTION(opt)                  mi_option_##opt, #opt, NULL
#define MI_OPTION_LEGACY(opt,legacy)    mi_option_##opt, #opt, #legacy

static mi_option_desc_t options[_mi_option_last] =
{
  // stable options
  { 0, MI_OPTION(show_errors) },
  { 0, MI_OPTION(show_stats) },
  { 0, MI_OPTION(verbose) },

  // some of the following options are experimental and not all combinations are allowed.
  { 1, MI_OPTION(eager_commit) },               // commit per segment directly (4MiB)  (but see also `eager_commit_delay`)
  { 2, MI_OPTION_LEGACY(arena_eager_commit,eager_region_commit) }, // eager commit arena's? 2 is used to enable this only on an OS that has overcommit (i.e. linux)
  { 1, MI_OPTION_LEGACY(purge_decommits,reset_decommits) },        // purge decommits memory (instead of reset) (note: on linux this uses MADV_DONTNEED for decommit)
  { 0, MI_OPTION_LEGACY(allow_large_os_pages,large_os_pages) },    // use large OS pages, use only with eager commit to prevent fragmentation of VMA's
  { 0, MI_OPTION(reserve_huge_os_pages) },      // per 1GiB huge pages
  {-1, MI_OPTION(reserve_huge_os_pages_at) },   // reserve huge pages at node N
  { 0, MI_OPTION(reserve_os_memory)     },      // reserve N KiB OS memory in advance (use `option_get_size`)
  { 0, MI_OPTION(deprecated_segment_cache) },   // cache N segments per thread
  { 0, MI_OPTION(deprecated_page_reset) },      // reset page memory on free
  { 0, MI_OPTION_LEGACY(abandoned_page_purge,abandoned_page_reset) }, // reset free page memory when a thread terminates
  { 0, MI_OPTION(deprecated_segment_reset) },   // reset segment memory on free (needs eager commit)
  { 1, MI_OPTION(eager_commit_delay) },         // the first N segments per thread are not eagerly committed (but per page in the segment on demand)
  { 100, MI_OPTION_LEGACY(purge_delay,reset_delay) }, // purge delay in milli-seconds
  { 0, MI_OPTION(use_numa_nodes) },             // 0 = use available numa nodes, otherwise use at most N nodes.
  { 0, MI_OPTION_LEGACY(disallow_os_alloc,limit_os_alloc) }, // 1 = do not use OS memory for allocation (but only reserved arenas)
  { 100, MI_OPTION(os_tag) },                   // only apple specific for now but might serve more or less related purpose
  { 32, MI_OPTION(max_errors) },                // maximum errors that are output
  { 32, MI_OPTION(max_warnings) },              // maximum warnings that are output
  { 10, MI_OPTION(max_segment_reclaim)},        // max. percentage of the abandoned segments to be reclaimed per try.
  { 0, MI_OPTION(destroy_on_exit)},             // release all OS memory on process exit; careful with dangling pointer or after-exit frees!
  #if (MI_INTPTR_SIZE>4)
  { 1024L * 1024L, MI_OPTION(arena_reserve) },  // reserve memory N KiB at a time (=1GiB) (use `option_get_size`)
  #else
  { 128L * 1024L, MI_OPTION(arena_reserve) },
  #endif
  { 10, MI_OPTION(arena_purge_mult) },          // purge delay multiplier for arena's
  { 1, MI_OPTION_LEGACY(purge_extend_delay, decommit_extend_delay) },
  { 0, MI_OPTION(abandoned_reclaim_on_free) },  // reclaim an abandoned segment on a free
  { 0, MI_OPTION(disallow_arena_alloc) },       // 1 = do not use arena's for allocation (except if using specific arena id's)
  { 400, MI_OPTION(retry_on_oom) },             // windows only: retry on out-of-memory for N milli seconds (=400), set to 0 to disable retries.
  { 0, MI_OPTION(visit_abandoned) },            // 1 = allow visiting heap blocks in abandoned segments; requires taking locks during reclaim.
  { 0, MI_OPTION(guarded_min) },                // only used when building with MI_GUARDED: minimal rounded object size for guarded objects
  { MI_GiB, MI_OPTION(guarded_max) },           // only used when building with MI_GUARDED: maximal rounded object size for guarded objects
  { 0, MI_OPTION(guarded_precise) },            // disregard minimal alignment requirement to always place guarded blocks exactly in front of a guard page (=0)
  { 4000, MI_OPTION(guarded_sample_rate)},      // 1 out of N allocations in the min/max range will be guarded (=4000)
  { 0, MI_OPTION(guarded_sample_seed)},
  { 0, MI_OPTION(target_segments_per_thread) }, // abandon segments beyond this point, or 0 to disable.
  { 10000, MI_OPTION(generic_collect) },        // collect heaps every N (=10000) generic allocation calls
};

inline void _mi_options_init(void) {}

inline void mi_options_print(void) mi_attr_noexcept {}

long _mi_option_get_fast(mi_option_t option) {
  mi_assert(option >= 0 && option < _mi_option_last);
  mi_option_desc_t* desc = &options[option];
  mi_assert(desc->option == option);  // index should match the option
  return desc->value;
}

mi_decl_nodiscard long mi_option_get(mi_option_t option) {
  mi_assert(option >= 0 && option < _mi_option_last);
  if (option < 0 || option >= _mi_option_last) return 0;
  mi_option_desc_t* desc = &options[option];
  mi_assert(desc->option == option);  // index should match the option
  return desc->value;
}

mi_decl_nodiscard long mi_option_get_clamp(mi_option_t option, long min, long max) {
  long x = mi_option_get(option);
  return (x < min ? min : (x > max ? max : x));
}

mi_decl_nodiscard size_t mi_option_get_size(mi_option_t option) {
  const long x = mi_option_get(option);
  size_t size = (x < 0 ? 0 : (size_t)x);
  if (option == mi_option_reserve_os_memory || option == mi_option_arena_reserve) {
    size *= MI_KiB;
  }
  return size;
}

mi_decl_nodiscard bool mi_option_is_enabled(mi_option_t option) {
  return (mi_option_get(option) != 0);
}

void mi_option_set(mi_option_t option, long value) {
  if (option != mi_option_purge_delay) return;
  mi_option_desc_t* desc = &options[option];
  mi_assert(desc->option == option);  // index should match the option
  desc->value = value;
}
#ifdef MI_really_secure
void mi_secure_option_set(mi_option_t, long) __attribute__((alias("mi_option_set")));
#endif

inline void mi_option_set_default(mi_option_t option, long value) {}
inline void mi_option_set_enabled(mi_option_t option, bool enable) {}
inline void mi_option_set_enabled_default(mi_option_t option, bool enable) {}
inline void mi_option_enable(mi_option_t option) {}
inline void mi_option_disable(mi_option_t option) {}

inline void mi_register_output(mi_output_fun* out, void* arg) mi_attr_noexcept {}
inline void _mi_fputs(mi_output_fun* out, void* arg, const char* prefix, const char* message) {}
inline void _mi_fprintf(mi_output_fun* out, void* arg, const char* fmt, ...) {}
inline void _mi_message(const char* fmt, ...) {}
inline void _mi_trace_message(const char* fmt, ...) {}
inline void _mi_verbose_message(const char* fmt, ...) {}
inline void _mi_warning_message(const char* fmt, ...) {}

#if MI_DEBUG
mi_decl_noreturn mi_decl_cold void _mi_assert_fail(const char* assertion, const char* fname, unsigned line, const char* func) mi_attr_noexcept {
  abort();
}
#endif

// --------------------------------------------------------
// Errors
// --------------------------------------------------------

static void mi_error_default(int err) {
  MI_UNUSED(err);
#if (MI_SECURE>0)
  if (err==EFAULT) {  // abort on serious errors in secure mode (corrupted meta-data)
    abort();
  }
#endif
#if defined(MI_XMALLOC)
  if (err==ENOMEM || err==EOVERFLOW) { // abort on memory allocation fails in xmalloc mode
    abort();
  }
#endif
}

void mi_register_error(mi_error_fun* fun, void* arg) {
  MI_UNUSED(fun);
}

void _mi_error_message(int err, const char* fmt, ...) {
  // show detailed error message
  va_list args;
  va_start(args, fmt);
  va_end(args);
  // and call the error handler which may abort (or return normally)
  mi_error_default(err);
}
