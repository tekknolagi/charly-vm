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

#include <iostream>
#include <optional>
#include <queue>
#include <map>
#include <atomic>
#include <chrono>

#include "defines.h"
#include "gc.h"
#include "instructionblock.h"
#include "internals.h"
#include "opcode.h"
#include "status.h"
#include "stringpool.h"
#include "symboltable.h"
#include "value.h"

#include "compiler-manager.h"
#include "compiler.h"
#include "parser.h"
#include "sourcefile.h"

#pragma once

namespace Charly {

struct VMInstructionProfileEntry {
  uint64_t encountered = 0;
  uint64_t average_length = 0;
};

// Stores how often each type of instruction was encountered
// and how it took on average
class VMInstructionProfile {
public:
  VMInstructionProfile() : entries(nullptr) {
    this->entries = new VMInstructionProfileEntry[OpcodeCount];
  }
  ~VMInstructionProfile() {
    delete[] this->entries;
  }

  void add_entry(Opcode opcode, uint64_t length) {
    VMInstructionProfileEntry& entry = this->entries[opcode];
    entry.average_length = (entry.average_length * entry.encountered + length) / (entry.encountered + 1);
    entry.encountered += 1;
  }

  VMInstructionProfileEntry* entries;
};

struct VMContext {
  Compilation::CompilerManager& compiler_manager;

  bool instruction_profile = false;
  bool trace_opcodes = false;
  bool trace_catchtables = false;
  bool trace_frames = false;
  bool trace_gc = false;
  bool verbose_addresses = false;

  std::vector<std::string>* argv;
  std::unordered_map<std::string, std::string>* environment;

  std::istream& in_stream = std::cin;
  std::ostream& out_stream = std::cout;
  std::ostream& err_stream = std::cerr;
};

/*
 * Stores information about a callback the VM needs to execute
 * */
struct VMTask {
  bool is_thread;
  uint64_t uid;
  union {
    struct {
      uint64_t id;
      VALUE argument;
    } thread;
    struct {
      VALUE func;
      VALUE arguments[4];
    } callback;
  };

  /*
   * Initialize a VMTask which resumes a thread
   * */
  static inline VMTask init_thread(uint64_t id, VALUE argument) {
    return { .is_thread = true, .uid = 0, .thread = { id, argument } };
  }

  /*
   * Initialize a VMTask which calls a callback, with up to 4 arguments
   * */
  static inline VMTask init_callback_with_id(uint64_t id,
                                     VALUE func,
                                     VALUE arg1 = kNull,
                                     VALUE arg2 = kNull,
                                     VALUE arg3 = kNull,
                                     VALUE arg4 = kNull) {
    return { .is_thread = false, .uid = id, .callback = { func, { arg1, arg2, arg3, arg4 } } };
  }
  static inline VMTask init_callback(VALUE func,
                                     VALUE arg1 = kNull,
                                     VALUE arg2 = kNull,
                                     VALUE arg3 = kNull,
                                     VALUE arg4 = kNull) {
    return VMTask::init_callback_with_id(0, func, arg1, arg2, arg3, arg4);
  }
};

/*
 * Suspended VM thread
 * */
struct VMThread {
  uint64_t uid;
  std::vector<VALUE> stack;
  Frame* frame;
  CatchTable* catchstack;
  uint8_t* resume_address;

  VMThread(uint64_t u, std::vector<VALUE>&& s, Frame* f, CatchTable* c, uint8_t* r)
      : uid(u), stack(std::move(s)), frame(f), catchstack(c), resume_address(r) {
  }
};

// Represents a worker thread started by the VM
struct WorkerThread {
  CFunction* cfunc;
  std::vector<VALUE> arguments;
  Function* callback;
  VALUE error_value;
  std::thread thread;

  WorkerThread(CFunction* _cf, const std::vector<VALUE>& _args, Function* _cb)
      : cfunc(_cf), arguments(_args), callback(_cb), error_value(kNull) {
  }

  ~WorkerThread() {
    if (std::this_thread::get_id() == this->thread.get_id()) {
      this->thread.detach();
    } else {
      if (this->thread.joinable())
        this->thread.join();
    }
  }
};

class VM {
  friend GarbageCollector;
  friend ManagedContext;

public:
  VM(VMContext& ctx)
      : context(ctx),
        gc(GarbageCollectorConfig{.trace = ctx.trace_gc, .out_stream = ctx.err_stream, .err_stream = ctx.err_stream},
           this),
        running(true),
        uid(0),
        frames(nullptr),
        catchstack(nullptr),
        ip(nullptr),
        halted(false) {
    this->main_thread_id = std::this_thread::get_id();
  }
  VM(const VM& other) = delete;
  VM(VM&& other) = delete;
  ~VM() {
    this->gc.do_collect();
  }

  // Methods that operate on the VM's frames
  Frame* pop_frame();
  Frame* create_frame(VALUE self, Function* calling_function, uint8_t* return_address, bool halt_after_return = false);
  Frame* create_frame(VALUE self,
                      Frame* parent_environment_frame,
                      uint32_t lvarcount,
                      uint8_t* return_address,
                      bool halt_after_return = false);

  // Stack manipulation
  VALUE pop_stack();
  void push_stack(VALUE value);

  // CatchStack manipulation
  CatchTable* create_catchtable(uint8_t* address);
  CatchTable* pop_catchtable();
  void unwind_catchstack(std::optional<VALUE> payload);

  // Methods to create new data types
  VALUE create_object(uint32_t initial_capacity);
  VALUE create_array(uint32_t initial_capacity);
  VALUE create_string(const char* data, uint32_t length);
  VALUE create_string(const std::string& str);
  VALUE create_string(MemoryBlock* block);
  VALUE create_weak_string(char* data, uint32_t length);
  VALUE create_empty_short_string();
  VALUE create_function(VALUE name,
                        uint8_t* body_address,
                        uint32_t argc,
                        uint32_t minimum_argc,
                        uint32_t lvarcount,
                        bool anonymous,
                        bool needs_arguments);
  VALUE create_cfunction(VALUE name, uint32_t argc, void* pointer, uint8_t thread_policy = kThreadMain);
  VALUE create_generator(VALUE name, uint8_t* resume_address, Function* boot_function);
  VALUE create_class(VALUE name);
  VALUE create_cpointer(void* data, void* destructor);

  // Methods to copy existing data types
  VALUE copy_value(VALUE value);
  VALUE deep_copy_value(VALUE value);
  VALUE copy_object(VALUE object);
  VALUE deep_copy_object(VALUE object);
  VALUE copy_array(VALUE array);
  VALUE deep_copy_array(VALUE array);
  VALUE copy_string(VALUE string);
  VALUE copy_function(VALUE function);
  VALUE copy_cfunction(VALUE cfunction);
  VALUE copy_generator(VALUE generator);

  // Arithmetics
  VALUE add(VALUE left, VALUE right);
  VALUE sub(VALUE left, VALUE right);
  VALUE mul(VALUE left, VALUE right);
  VALUE div(VALUE left, VALUE right);
  VALUE mod(VALUE left, VALUE right);
  VALUE pow(VALUE left, VALUE right);
  VALUE uadd(VALUE value);
  VALUE usub(VALUE value);

  // Comparison operators
  VALUE eq(VALUE left, VALUE right);
  VALUE neq(VALUE left, VALUE right);
  VALUE lt(VALUE left, VALUE right);
  VALUE gt(VALUE left, VALUE right);
  VALUE le(VALUE left, VALUE right);
  VALUE ge(VALUE left, VALUE right);
  VALUE unot(VALUE value);

  // Bitwise operators
  VALUE shl(VALUE left, VALUE right);
  VALUE shr(VALUE left, VALUE right);
  VALUE band(VALUE left, VALUE right);
  VALUE bor(VALUE left, VALUE right);
  VALUE bxor(VALUE left, VALUE right);
  VALUE ubnot(VALUE value);

  // Machine functionality
  VALUE readmembersymbol(VALUE source, VALUE symbol);
  VALUE setmembersymbol(VALUE target, VALUE symbol, VALUE value);
  VALUE readmembervalue(VALUE source, VALUE value);
  VALUE setmembervalue(VALUE target, VALUE member_value, VALUE value);
  std::optional<VALUE> findprimitivevalue(VALUE value, VALUE symbol);
  void call(uint32_t argc, bool with_target, bool halt_after_return = false);
  void call_function(Function* function, uint32_t argc, VALUE* argv, VALUE self, bool halt_after_return = false);
  void call_cfunction(CFunction* function, uint32_t argc, VALUE* argv);
  void call_class(Class* klass, uint32_t argc, VALUE* argv);
  void call_generator(Generator* klass, uint32_t argc, VALUE* argv);
  void initialize_member_properties(Class* klass, Object* object);
  void throw_exception(const std::string& message);
  void throw_exception(VALUE payload);
  void panic(STATUS reason);
  void stackdump(std::ostream& io);
  void pretty_print(std::ostream& io, VALUE value);
  void to_s(std::ostream& io, VALUE value, uint32_t depth = 0);
  VALUE get_self_for_function(Function* function, const VALUE* fallback_ptr);
  VALUE get_global_self();
  VALUE get_global_symbol(VALUE symbol);
  Function* get_active_function();

  // Private member access
  inline Frame* get_current_frame() {
    return this->frames;
  }

  // Instructions
  Opcode fetch_instruction();
  void op_readlocal(uint32_t index, uint32_t level);
  void op_readmembersymbol(VALUE symbol);
  void op_readmembervalue();
  void op_readarrayindex(uint32_t index);
  void op_readglobal(VALUE symbol);
  void op_setlocalpush(uint32_t index, uint32_t level);
  void op_setmembersymbolpush(VALUE symbol);
  void op_setmembervaluepush();
  void op_setarrayindexpush(uint32_t index);
  void op_setlocal(uint32_t index, uint32_t level);
  void op_setmembersymbol(VALUE symbol);
  void op_setmembervalue();
  void op_setarrayindex(uint32_t index);
  void op_setglobal(VALUE symbol);
  void op_setglobalpush(VALUE symbol);
  void op_putself();
  void op_putsuper();
  void op_putsupermember(VALUE symbol);
  void op_putvalue(VALUE value);
  void op_putstring(char* data, uint32_t length);
  void op_putfunction(VALUE symbol,
                      uint8_t* body_address,
                      bool anonymous,
                      bool needs_arguments,
                      uint32_t argc,
                      uint32_t minimum_argc,
                      uint32_t lvarcount);
  void op_putgenerator(VALUE symbol, uint8_t* resume_address);
  void op_putarray(uint32_t count);
  void op_puthash(uint32_t count);
  void op_putclass(VALUE name,
                   uint32_t propertycount,
                   uint32_t staticpropertycount,
                   uint32_t methodcount,
                   uint32_t staticmethodcount,
                   bool has_parent_class,
                   bool has_constructor);
  void op_pop();
  void op_dup();
  void op_dupn(uint32_t count);
  void op_swap();
  void op_call(uint32_t argc);
  void op_callmember(uint32_t argc);
  void op_new(uint32_t argc);
  void op_return();
  void op_yield();
  void op_throw();
  void op_registercatchtable(int32_t offset);
  void op_popcatchtable();
  void op_branch(int32_t offset);
  void op_branchif(int32_t offset);
  void op_branchunless(int32_t offset);
  void op_branchlt(int32_t offset);
  void op_branchgt(int32_t offset);
  void op_branchle(int32_t offset);
  void op_branchge(int32_t offset);
  void op_brancheq(int32_t offset);
  void op_branchneq(int32_t offset);
  void op_typeof();

  VMContext context;
  VMInstructionProfile instruction_profile;

  inline uint8_t* get_ip() {
    return this->ip;
  }

  void run();
  uint8_t start_runtime();
  void exit(uint8_t status_code);
  uint64_t get_thread_uid();
  uint64_t get_next_thread_uid();
  void suspend_thread();
  void resume_thread(uint64_t uid, VALUE argument);
  void register_task(VMTask task);
  bool pop_task(VMTask* target);
  void clear_task_queue();
  VALUE register_module(InstructionBlock* block);
  uint64_t register_timer(Timestamp, VMTask task);
  uint64_t register_ticker(uint32_t, VMTask task);
  uint64_t get_next_timer_id();
  void clear_timer(uint64_t uid);
  void clear_ticker(uint64_t uid);

  WorkerThread* start_worker_thread(CFunction* cfunc, const std::vector<VALUE>& args, Function* callback);
  void close_worker_thread(WorkerThread* thread, VALUE return_value);
  void handle_worker_thread_exception(const std::string& message);

  // Check wether calling thread is main / worker
  bool is_main_thread();
  bool is_worker_thread();

  std::chrono::time_point<std::chrono::high_resolution_clock> starttime;
private:

  uint8_t status_code = 0;

  GarbageCollector gc;

  // Used to avoid an overflow when printing cyclic data structures
  std::vector<VALUE> pretty_print_stack;

  // References to the primitive classes of the VM
  VALUE primitive_array     = kNull;
  VALUE primitive_boolean   = kNull;
  VALUE primitive_class     = kNull;
  VALUE primitive_function  = kNull;
  VALUE primitive_generator = kNull;
  VALUE primitive_null      = kNull;
  VALUE primitive_number    = kNull;
  VALUE primitive_object    = kNull;
  VALUE primitive_string    = kNull;
  VALUE primitive_value     = kNull;

  // A function which handles uncaught exceptions
  VALUE uncaught_exception_handler = kNull;

  // Error class used by the VM
  VALUE internal_error_class = kNull;

  // Object which contains all the global variables
  VALUE globals = kNull;

  // Scheduled tasks and paused VM threads
  uint64_t next_thread_id = 0;
  std::map<uint64_t, VMThread> paused_threads;
  std::queue<VMTask> task_queue;
  std::mutex task_queue_m;
  std::condition_variable task_queue_cv;
  std::atomic<bool> running;

  // Remaining timers & tickers
  std::map<Timestamp, VMTask> timers;
  std::map<Timestamp, std::tuple<VMTask, uint32_t>> tickers;

  uint64_t next_timer_id = 0;

  // Worker threads
  std::mutex worker_threads_m;
  std::unordered_map<std::thread::id, WorkerThread*> worker_threads;
  std::thread::id main_thread_id;

  // The uid of the current thread of execution
  uint64_t uid;

  std::queue<VALUE> pop_queue;
  std::vector<VALUE> stack;
  Frame* frames;
  CatchTable* catchstack;
  uint8_t* ip;
  bool halted;
};
}  // namespace Charly
