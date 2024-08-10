/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define MI_BIONIC 1

#if defined(MI_SECURE) && (MI_SECURE >= 3)
#define MI_really_secure 1
#endif

/* use smaller segments to accommodate smaller arenas */
#define MI_SEGMENT_SHIFT (7 + MI_SEGMENT_SLICE_SHIFT)

#include "mimalloc.h"
#include "mimalloc/internal.h"

// For a static override we create a single object file
// containing the whole library. If it is linked first
// it will override all the standard library allocation
// functions (on Unix's).
#include "alloc.c"          // includes alloc-override.c
#include "alloc-aligned.c"
#include "alloc-posix.c"
#include "arena.c"
#include "bitmap.c"
#include "heap.c"
#include "init.c"
#include "libc.c"
#include "options-stub.c"
#include "os.c"
#include "page.c"           // includes page-queue.c
#include "random.c"
#include "segment.c"
#include "segment-map.c"
#include "stats.c"
#include "prim/prim.c"
