// Copyright (c) 2016 Sean Gillespie
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// afurnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include "gc.h"
#include "contract.h"
#include <cstdarg>
#include <list>
#include <stack>
#include <unordered_map>

#ifdef _WIN32
#include "windows.h"
#else
#include <sys/mman.h>
#endif

#ifdef DEBUG
#define GC_DEBUG_LOG_SIZE 256
#define GC_DEBUG_MSG_SIZE 256

char *g_gc_log[GC_DEBUG_LOG_SIZE];
size_t g_gc_log_index = 0;
#endif

#include <unordered_set>

Frame *g_frames;
Frame *g_current_frame;
GcHeap *g_heap;
Sexp g_the_empty_sexp;

// Useful for debugging GC issues when
// you have a repro
//#define DEBUG_LOG_TO_STDOUT

void DebugLog(const char *fmt, ...) {
#if defined(DEBUG) && !defined(DEBUG_LOG_TO_STDOUT)
  // for troubleshooting GC issues, we maintain a circular buffer
  // of logs that we can inspect in a debugger.
  if (g_gc_log[g_gc_log_index]) {
    // free the existing message before overwriting it.
    delete[] g_gc_log[g_gc_log_index];
  }

  g_gc_log[g_gc_log_index] = new char[GC_DEBUG_MSG_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(g_gc_log[g_gc_log_index], GC_DEBUG_MSG_SIZE, fmt, args);
  va_end(args);
  g_gc_log_index = (g_gc_log_index + 1) % GC_DEBUG_LOG_SIZE;
#elif defined(DEBUG) && defined(DEBUG_LOG_TO_STDOUT)
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
#else
  UNUSED_PARAMETER(fmt);
#endif
}

template <typename F> void ScanRoots(F func) {
  // the VM maintains a linked list of native frames that may contain managed
  // pointers.
  assert(g_frames != nullptr);
  for (Frame *frame = g_current_frame; frame != nullptr;
       frame = frame->GetParent()) {
    DebugLog("scanning roots for frame '%s'", frame->GetName());
    // each frame in turn maintains a list of rooted pointers.
    frame->TracePointers([&](std::tuple<const char *, Sexp **> root) {
      // roots may be null, since a GC may occur before a GC reference is
      // assigned to a protected slot. This is normal and OK.
      DebugLog("  found root: %s (%p)", std::get<0>(root), *std::get<1>(root));
      if (std::get<1>(root) != nullptr) {
        func(std::get<1>(root));
      }
    });
  }
}

// Maps the heap, calling whatever platform APIs we need to do so.
// On Unixes, we map the heap using mmap. On Windows, we'll use VirtualAlloc.
uint8_t *MapTheHeap(size_t page_number) {
#ifndef _WIN32
  void *heap = mmap(nullptr, PAGE_SIZE * page_number, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (heap == (void *)-1) {
    std::string msg(strerror(errno));
    std::string result = "failed to allocate heap: " + msg;
    PANIC(result.c_str());
  }

  return (uint8_t *)heap;
#else
  void *heap = VirtualAlloc(nullptr, PAGE_SIZE * page_number,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (heap == nullptr) {
    std::ostringstream stream;
    stream << "failed to allocate heap: error " << GetLastError();
    PANIC(stream.str().c_str());
  }

  return (uint8_t *)heap;
#endif
}

// Unmaps the heap, either by calling munmap or VirtualFree.
void UnmapTheHeap(uint8_t *heap, size_t size) {
#ifndef _WIN32
  munmap(heap, size);
#else
  UNUSED_PARAMETER(size);
  VirtualFree(heap, 0, MEM_RELEASE);
#endif
}

// Jet's GC is a semispace copying collector. It partitions
// the heap into two distinct regions: the "fromspace" and "tospace".
// When a GC occurs, the two regions are swapped and all live objects
// are copied to the new semispace.
class GcHeap::GcHeapImpl {
private:
  // TODO(segilles) perf tweak this number.
  const size_t number_of_pages = 8;
  uint8_t *tospace;
  uint8_t *fromspace;
  uint8_t *top;
  uint8_t *free;
  uint8_t *heap_start;
  uint8_t *heap_end;
  std::unordered_map<Sexp *, Sexp *> forwarding_addresses;
  std::vector<Sexp *> worklist;
  size_t extent;
  size_t gc_number;
  bool stress;
  std::list<Sexp *> finalize_queue;

public:
  GcHeapImpl() {
    uint8_t *heap = MapTheHeap(number_of_pages);
    heap_start = (uint8_t *)heap;
    heap_end = heap_start + PAGE_SIZE * number_of_pages;
    tospace = heap_start;
    extent = (heap_end - heap_start) / 2;
    fromspace = heap_start + extent;
    top = fromspace;
    free = tospace;
    stress = false;
    gc_number = 0;
    assert(heap_start < heap_end);
  }

  ~GcHeapImpl() { UnmapTheHeap(heap_start, PAGE_SIZE * number_of_pages); }

  GcHeapImpl(const GcHeapImpl &) = delete;
  GcHeapImpl &operator=(const GcHeapImpl &) = delete;

  // Allocates an s-expression from the heap, triggering a garbage collection if
  // necessary.
  Sexp *Allocate(bool should_finalize) {
    // on a debug build, this triggers a stackwalk which will assert if
    // any of the calling functions has a FORBID_GC contract.
    CONTRACT_VIOLATIONS { PERFORMS_GC; }

    uint8_t *result = free;
    uint8_t *bump = result + sizeof(Sexp);
    if (stress || bump > top) {
      DebugLog("bump pointer alloc failed, triggering a GC");
      // we've filled up our fromspace - need to GC.
      Collect();

      // try and allocate again.
      result = free;
      bump = result + sizeof(Sexp);
      if (bump > top) {
        // we're screwed. OOM.
        // TODO(segilles) - we should be able to try and expand the
        // heap here.
        PANIC("out of memory!");
      }
    }

    DebugLog("allocated object at %p, new bump at %p", result, bump);
    free = bump;

    memset(result, 0x0, sizeof(Sexp));

    // if this s-expression needs to be finalized,
    // stick it on the queue.
    if (should_finalize) {
      DebugLog("marking object %p for finalization", result);
      finalize_queue.push_back((Sexp *)result);
    }

    return (Sexp *)result;
  }

  // Performs a garbage collection.
  void Collect() {
#ifdef DEBUG
    VerifyHeap();
#endif

    gc_number++;
    DebugLog("[%d] beginning a GC", gc_number);
    assert(forwarding_addresses.empty());
    assert(worklist.empty());
    // flip the fromspace and tospace - we're about
    // to relocate all of our live objects to the new tospace.
    Flip();

    // all roots are known to be live. we'll process those first.
    DebugLog("[%d] processing roots", gc_number);
    ScanRoots([&](Sexp **ptr) { Process(ptr); });

    // we've populated our worklist, now we need to process it.
    DebugLog("[%d] draining worklist", gc_number);
    while (!worklist.empty()) {
      Sexp *ptr = worklist.back();
      worklist.pop_back();
      // this pointer has already been relocated - we have to
      // process its transitive closure now.
      ptr->TracePointers([&](Sexp **ref) {
        uint8_t *candidate = (uint8_t *)*ref;
        if (candidate >= tospace && candidate < top) {
          // if this pointer points into tospace, that means
          // we've already relocated it and updated the pointer
          // in this object. we don't need to process it again.
          // in fact, we /can't/ process it again, because if we
          // do, it'll get moved to some other garbage location.
          return;
        }

        Process(ref);
      });
    }

    DebugLog("[%d] finalizing dead objects", gc_number);

    finalize_queue.remove_if([&](Sexp *ptr) {
      // check and see if this pointer got relocated. If it
      // did, it has an entry in the forwarding address table.
      if (forwarding_addresses.find(ptr) == forwarding_addresses.end()) {
        // this is correct because, since the object did not relocate,
        // it's still safe to refer to this object by its fromspace pointer.
        DebugLog("[%d] finalizing object %p", gc_number, ptr);
        ptr->Finalize();
        return true;
      }

      // if the object did relocate, it's still live.
      return false;
    });

    DebugLog("[%d] updating pointers in finalizer queue", gc_number);
    // for everything still live in the finalizer queue, we have to
    // update it to point to its relocated location.
    for (auto it = finalize_queue.begin(); it != finalize_queue.end();) {
      Sexp *forward_ptr = forwarding_addresses[*it];
      assert(forward_ptr != nullptr);
      DebugLog("[%d] finalizer queue relocation: %p -> %p", gc_number, *it,
               forward_ptr);
      it = finalize_queue.erase(it);
      finalize_queue.insert(it, forward_ptr);
    }

    DebugLog("[%d] GC complete", gc_number);
    // and we're done!
    forwarding_addresses.clear();
    worklist.clear();

#ifdef DEBUG
    VerifyHeap();
#endif
  }

  // Update a field with a reference to a tospace replica.
  void Process(Sexp **ptr) {
    // nothing to do for null pointers.
    if (ptr == nullptr || *ptr == nullptr) {
      return;
    }

    if ((*ptr)->IsEmpty()) {
      // the empty list is special and isn't managed
      // by the GC.
      return;
    }

    assert((uint8_t *)*ptr >= heap_start && (uint8_t *)*ptr <= heap_end);
    *ptr = Forward(*ptr);
  }

  // Copies an object from fromspace to tospace, returning
  // the forwarded pointer to this object.
  Sexp *Forward(Sexp *ptr) {
    Sexp *to_ref = forwarding_addresses[ptr];
    if (to_ref == nullptr) {
      // this reference hasn't been copied yet. do it.
      to_ref = Copy(ptr);
    }

    // DebugLog("processed value: %s", to_ref->DumpString().c_str());
    assert(to_ref != nullptr);
    return to_ref;
  }

  // Copy an object and return its forwarding address.
  Sexp *Copy(Sexp *from_ref) {
    Sexp *to_ref = (Sexp *)free;
    free += sizeof(Sexp);
    // this copy is guaranteed not to overlap since it
    // doesn't cross the fromspace/tospace boundary.
    DebugLog("[%d] relocating: %p -> %p", gc_number, from_ref, to_ref);
#ifdef DEBUG
    assert(from_ref->padding != 0xabababababababab &&
           "relocating an invalid object");
#endif
    memcpy(to_ref, from_ref, sizeof(Sexp));

#ifdef DEBUG
    // use a distinct bit pattern to ensure that
    // we insta-crash on a GC hole.
    memset(from_ref, 0xAB, sizeof(Sexp));
#endif

    forwarding_addresses[from_ref] = to_ref;
    worklist.push_back(to_ref);
    return to_ref;
  }

  // Flips the fromspace and tospace during a GC.
  void Flip() {
    std::swap(fromspace, tospace);
    top = tospace + extent;
    free = tospace;
  }

  void ToggleStress() { stress = !stress; }

  void VerifyHeap() {
// The basic idea here is to traverse the entire
// heap and verify that every pointer is 1) within
// the valid heap range (i.e. we didn't accidentally introduce
// any wild pointers during a GC) and to ensure that every pointer
// points to a non-relocated object.
//
// When an object is relocated in a debug build, it is replaced
// with the bit pattern 0xab - if we see a pointer here that
// observes that bit pattern, we know that we failed to update
// a pointer somewhere.
#ifdef DEBUG
    DebugLog("[%d] verifying heap", gc_number);
    std::vector<Sexp *> stack;
    std::unordered_set<Sexp *> visited;
    ScanRoots([&](Sexp **ptr) {
      if (ptr == nullptr || *ptr == nullptr) {
        return;
      }

      if ((*ptr)->IsEmpty()) {
        return;
      }

      stack.push_back(*ptr);
    });

    // make sure we relocated the finalize queue right
    for (auto &it : finalize_queue) {
      stack.push_back(it);
    }

    while (!stack.empty()) {
      Sexp *ptr = stack.back();
      DebugLog("processing: %p", ptr);
      stack.pop_back();
      if (visited.count(ptr) != 0) {
        continue;
      }

      visited.insert(ptr);
      assert((uint8_t *)ptr >= heap_start && (uint8_t *)ptr <= heap_end &&
             "pointer not in heap!");
      assert(ptr->padding != 0xabababababababab &&
             "observed a pointer that has been relocated!");
      ptr->TracePointers([&](Sexp **child) {
        if ((*child)->IsEmpty()) {
          return;
        }

        stack.push_back(*child);
      });

      // DebugLog("heap verify: object %s is reachable",
      // ptr->DumpString().c_str());
    }
#endif
  }
};

GcHeap::GcHeap() { pimpl = std::make_unique<GcHeap::GcHeapImpl>(); }

GcHeap::~GcHeap() {}

Sexp *GcHeap::Allocate(bool should_finalize) {
  return pimpl->Allocate(should_finalize);
}

void GcHeap::Collect() {
  CONTRACT_VIOLATIONS { PERFORMS_GC; }
  return pimpl->Collect();
}

void GcHeap::ToggleStress() { return pimpl->ToggleStress(); }
