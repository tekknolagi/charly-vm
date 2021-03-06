/*
 * This file is part of the Charly Virtual Machine (https://github.com/KCreate/charly-vm)
 *
 * MIT License
 *
 * Copyright (c) 2017 - 2020 Leonard Schütz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cassert>
#include <iostream>

#include "gc.h"
#include "vm.h"

namespace Charly {
void GarbageCollector::add_heap() {
  MemoryCell* heap = static_cast<MemoryCell*>(std::calloc(this->config.heap_cell_count, sizeof(MemoryCell)));
  this->heaps.push_back(heap);
  this->remaining_free_cells += this->config.heap_cell_count;

  // Add the newly allocated cells to the free list
  MemoryCell* last_cell = this->free_cell;
  for (size_t i = 0; i < this->config.heap_cell_count; i++) {
    heap[i].free.next = last_cell;
    last_cell = heap + i;
  }

  this->free_cell = last_cell;
}

void GarbageCollector::grow_heap() {
  size_t heap_count = this->heaps.size();
  size_t heaps_to_add = (heap_count * this->config.heap_growth_factor + 1) - heap_count;
  while (heaps_to_add--)
    this->add_heap();
}

void GarbageCollector::mark_persistent(VALUE value) {
  std::unique_lock<std::recursive_mutex> g_lock(this->g_mutex);
  this->temporaries.insert(value);
}

void GarbageCollector::unmark_persistent(VALUE value) {
  std::unique_lock<std::recursive_mutex> g_lock(this->g_mutex);

  // Check if this value is a registered temporary variable
  if (this->temporaries.count(value)) {
    auto it = this->temporaries.find(value);
    if (it != this->temporaries.end()) {
      this->temporaries.erase(it);
    }
  }
}

void GarbageCollector::mark(VALUE value) {
  if (!charly_is_ptr(value)) {
    return;
  }

  if (charly_as_pointer(value) == nullptr) {
    return;
  }

  if (charly_as_basic(value)->mark) {
    return;
  }

  charly_as_basic(value)->mark = true;
  switch (charly_as_basic(value)->type) {
    case kTypeObject: {
      Object* obj = charly_as_object(value);
      this->mark(obj->klass);
      for (auto entry : *obj->container)
        this->mark(entry.second);
      break;
    }

    case kTypeArray: {
      Array* arr = charly_as_array(value);
      for (auto arr_entry : *arr->data)
        this->mark(arr_entry);
      break;
    }

    case kTypeFunction: {
      Function* func = charly_as_function(value);
      this->mark(charly_create_pointer(func->context));
      this->mark(func->host_class);

      if (func->bound_self_set)
        this->mark(func->bound_self);
      for (auto entry : *func->container)
        this->mark(entry.second);
      break;
    }

    case kTypeCFunction: {
      CFunction* cfunc = charly_as_cfunction(value);
      for (auto entry : *cfunc->container)
        this->mark(entry.second);
      break;
    }

    case kTypeGenerator: {
      Generator* gen = charly_as_generator(value);
      // We only mark these values if the generator is still running
      if (!gen->finished()) {
        this->mark(charly_create_pointer(gen->context_frame));
        this->mark(charly_create_pointer(gen->boot_function));
        this->mark(charly_create_pointer(gen->context_catchtable));
        if (gen->bound_self_set)
          this->mark(gen->bound_self);
        for (auto entry : *gen->context_stack)
          this->mark(entry);
      }
      for (auto entry : *gen->container)
        this->mark(entry.second);
      break;
    }

    case kTypeClass: {
      Class* klass = charly_as_class(value);
      this->mark(klass->constructor);
      this->mark(klass->prototype);
      this->mark(klass->parent_class);
      for (auto entry : *klass->container)
        this->mark(entry.second);
      break;
    }

    case kTypeFrame: {
      Frame* frame = charly_as_frame(value);
      this->mark(charly_create_pointer(frame->parent));
      this->mark(charly_create_pointer(frame->parent_environment_frame));
      this->mark(charly_create_pointer(frame->last_active_catchtable));
      this->mark(frame->caller_value);
      this->mark(frame->self);

      for (size_t i = 0; i < frame->lvarcount(); i++) {
        this->mark(frame->read_local(i));
      }

      break;
    }

    case kTypeCatchTable: {
      CatchTable* table = charly_as_catchtable(value);
      this->mark(charly_create_pointer(table->frame));
      this->mark(charly_create_pointer(table->parent));
      break;
    }
  }
}

void GarbageCollector::do_collect() {
  std::unique_lock<std::recursive_mutex> g_lock(this->g_mutex);
  this->collect();
}

void GarbageCollector::collect() {
  std::unique_lock<std::recursive_mutex>(this->g_mutex);

  auto gc_start_time = std::chrono::high_resolution_clock::now();
  if (this->config.trace) {
    this->config.out_stream << "#-- GC: Pause --#" << '\n';
  }

  if (this->host_vm->running) {

    // Top level values
    this->mark(charly_create_pointer(this->host_vm->frames));
    this->mark(charly_create_pointer(this->host_vm->catchstack));
    this->mark(this->host_vm->uncaught_exception_handler);
    this->mark(this->host_vm->internal_error_class);
    this->mark(this->host_vm->globals);
    this->mark(this->host_vm->primitive_array);
    this->mark(this->host_vm->primitive_boolean);
    this->mark(this->host_vm->primitive_class);
    this->mark(this->host_vm->primitive_function);
    this->mark(this->host_vm->primitive_generator);
    this->mark(this->host_vm->primitive_null);
    this->mark(this->host_vm->primitive_number);
    this->mark(this->host_vm->primitive_object);
    this->mark(this->host_vm->primitive_string);
    this->mark(this->host_vm->primitive_value);

    // Stack
    for (VALUE item : this->host_vm->stack) {
      this->mark(item);
    }

    while (this->host_vm->pop_queue.size()) {
      VALUE front = this->host_vm->pop_queue.front();
      this->host_vm->pop_queue.pop();
      this->mark(front);
    }

    // Task queue
    {
      std::lock_guard<std::mutex> lock(this->host_vm->task_queue_m);
      auto task_queue_copy = this->host_vm->task_queue;
      while (task_queue_copy.size()) {
        VMTask task = task_queue_copy.front();
        task_queue_copy.pop();

        if (task.is_thread) {
          this->mark(task.thread.argument);
        } else {
          this->mark(task.callback.func);
          this->mark(task.callback.arguments[0]);
          this->mark(task.callback.arguments[1]);
          this->mark(task.callback.arguments[2]);
          this->mark(task.callback.arguments[3]);
        }
      }
    }

    // Timers
    for (auto& it : this->host_vm->timers) {
      this->mark(it.second.callback.func);
      this->mark(it.second.callback.arguments[0]);
      this->mark(it.second.callback.arguments[1]);
      this->mark(it.second.callback.arguments[2]);
      this->mark(it.second.callback.arguments[3]);
    }

    // Tickers
    for (auto& it : this->host_vm->tickers) {
      this->mark(std::get<0>(it.second).callback.func);
      this->mark(std::get<0>(it.second).callback.arguments[0]);
      this->mark(std::get<0>(it.second).callback.arguments[1]);
      this->mark(std::get<0>(it.second).callback.arguments[2]);
      this->mark(std::get<0>(it.second).callback.arguments[3]);
    }

    // Paused threads
    for (auto& it : this->host_vm->paused_threads) {
      VMThread& thread = it.second;
      for (VALUE v : thread.stack) {
        this->mark(v);
      }
      this->mark(charly_create_pointer(thread.frame));
      this->mark(charly_create_pointer(thread.catchstack));
    }

    // Worker threads
    {
      std::lock_guard<std::mutex> lock(this->host_vm->worker_threads_m);
      for (auto& entry : this->host_vm->worker_threads) {
        if (entry.second->cfunc)
          this->mark(charly_create_pointer(entry.second->cfunc));
        if (entry.second->callback)
          this->mark(charly_create_pointer(entry.second->callback));
        this->mark(entry.second->error_value);
        for (VALUE val : entry.second->arguments) {
          this->mark(val);
        }
      }
    }
  }

  // Mark all temporaries
  for (auto temp_item_iter : this->temporaries) {
    this->mark(temp_item_iter);
  }

  // Sweep Phase
  int freed_cells_count = 0;
  for (MemoryCell* heap : this->heaps) {
    for (size_t i = 0; i < this->config.heap_cell_count; i++) {
      MemoryCell* cell = heap + i;
      if (charly_as_basic(charly_create_pointer(cell))->mark) {
        charly_as_basic(charly_create_pointer(cell))->mark = false;
      } else {
        // This cell might already be on the free list
        // Make sure we don't double free cells
        if (!charly_is_dead(charly_create_pointer(cell))) {
          freed_cells_count++;
          this->deallocate(cell);
        }
      }
    }
  }

  if (this->config.trace) {
    std::chrono::duration<double> gc_collect_duration = std::chrono::high_resolution_clock::now() - gc_start_time;
    this->config.out_stream << std::fixed;
    this->config.out_stream << std::setprecision(0);
    this->config.out_stream << "#-- GC: Freed " << (freed_cells_count * sizeof(MemoryCell)) << " bytes --#" << '\n';
    this->config.out_stream << "#-- GC: Finished in " << gc_collect_duration.count() * 1000000000 << " nanoseconds --#"
                            << '\n';
    this->config.out_stream << std::setprecision(6);
  }
}

MemoryCell* GarbageCollector::allocate() {
  std::unique_lock<std::recursive_mutex> g_lock(this->g_mutex);

  MemoryCell* cell = this->free_cell;
  this->free_cell = this->free_cell->free.next;

  // If we've just allocated the last available cell,
  // we do a collect in order to make sure we never get a failing
  // allocation in the future
  if (this->free_cell == nullptr || this->remaining_free_cells <= this->config.min_free_cells) {
    this->collect();

    // If a collection didn't yield new available space,
    // allocate more heaps
    if (this->free_cell == nullptr) {
      this->grow_heap();

      if (!this->free_cell) {
        this->config.err_stream << "Failed to expand heap, the next allocation will cause a segfault." << '\n';
      }
    }
  }

  this->remaining_free_cells--;
  return cell;
}

void GarbageCollector::deallocate(MemoryCell* cell) {
  std::unique_lock<std::recursive_mutex>(this->g_mutex);

  // Run the type specific cleanup function
  switch (charly_as_basic(charly_create_pointer(cell))->type) {
    case kTypeObject: {
      cell->object.clean();
      break;
    }

    case kTypeArray: {
      cell->array.clean();
      break;
    }

    case kTypeString: {
      cell->string.clean();
      break;
    }

    case kTypeFunction: {
      cell->function.clean();
      break;
    }

    case kTypeCFunction: {
      cell->cfunction.clean();
      break;
    }

    case kTypeGenerator: {
      cell->generator.clean();
      break;
    }

    case kTypeClass: {
      cell->klass.clean();
      break;
    }

    case kTypeCPointer: {
      cell->cpointer.clean();
      break;
    }

    case kTypeFrame: {
      cell->frame.clean();
      break;
    }

    default: { break; }
  }

  // Clear the cell and link it into the freelist
  memset(reinterpret_cast<void*>(cell), 0, sizeof(MemoryCell));
  cell->free.basic.type = kTypeDead;
  cell->free.next = this->free_cell;
  this->free_cell = cell;
  this->remaining_free_cells++;
}

void GarbageCollector::lock() {
  this->g_mutex.lock();
}

void GarbageCollector::unlock() {
  this->g_mutex.unlock();
}

}  // namespace Charly
