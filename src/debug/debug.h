// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_H_
#define V8_DEBUG_DEBUG_H_

#include "src/allocation.h"
#include "src/arguments.h"
#include "src/assembler.h"
#include "src/base/atomicops.h"
#include "src/base/hashmap.h"
#include "src/base/platform/platform.h"
#include "src/debug/debug-interface.h"
#include "src/debug/interface-types.h"
#include "src/execution.h"
#include "src/factory.h"
#include "src/flags.h"
#include "src/frames.h"
#include "src/globals.h"
#include "src/runtime/runtime.h"
#include "src/source-position-table.h"
#include "src/string-stream.h"
#include "src/v8threads.h"

#include "include/v8-debug.h"

namespace v8 {
namespace internal {


// Forward declarations.
class DebugScope;


// Step actions. NOTE: These values are in macros.py as well.
enum StepAction : int8_t {
  StepNone = -1,  // Stepping not prepared.
  StepOut = 0,    // Step out of the current function.
  StepNext = 1,   // Step to the next statement in the current function.
  StepIn = 2,     // Step into new functions invoked or the next statement
                  // in the current function.
  LastStepAction = StepIn
};

// Type of exception break. NOTE: These values are in macros.py as well.
enum ExceptionBreakType {
  BreakException = 0,
  BreakUncaughtException = 1
};


// Type of exception break.
enum BreakLocatorType { ALL_BREAK_LOCATIONS, CALLS_AND_RETURNS };


// The different types of breakpoint position alignments.
// Must match Debug.BreakPositionAlignment in debug.js
enum BreakPositionAlignment {
  STATEMENT_ALIGNED = 0,
  BREAK_POSITION_ALIGNED = 1
};

enum DebugBreakType {
  NOT_DEBUG_BREAK,
  DEBUGGER_STATEMENT,
  DEBUG_BREAK_SLOT,
  DEBUG_BREAK_SLOT_AT_CALL,
  DEBUG_BREAK_SLOT_AT_RETURN,
  DEBUG_BREAK_SLOT_AT_TAIL_CALL,
};

class BreakLocation {
 public:
  static BreakLocation FromFrame(Handle<DebugInfo> debug_info,
                                 JavaScriptFrame* frame);

  static void AllAtCurrentStatement(Handle<DebugInfo> debug_info,
                                    JavaScriptFrame* frame,
                                    List<BreakLocation>* result_out);

  inline bool IsReturn() const { return type_ == DEBUG_BREAK_SLOT_AT_RETURN; }
  inline bool IsCall() const { return type_ == DEBUG_BREAK_SLOT_AT_CALL; }
  inline bool IsTailCall() const {
    return type_ == DEBUG_BREAK_SLOT_AT_TAIL_CALL;
  }
  inline bool IsDebugBreakSlot() const { return type_ >= DEBUG_BREAK_SLOT; }
  inline bool IsDebuggerStatement() const {
    return type_ == DEBUGGER_STATEMENT;
  }

  bool HasBreakPoint(Handle<DebugInfo> debug_info) const;

  inline int position() const { return position_; }

 private:
  BreakLocation(Handle<AbstractCode> abstract_code, DebugBreakType type,
                int code_offset, int position)
      : abstract_code_(abstract_code),
        code_offset_(code_offset),
        type_(type),
        position_(position) {
    DCHECK_NE(NOT_DEBUG_BREAK, type_);
  }

  static int BreakIndexFromCodeOffset(Handle<DebugInfo> debug_info,
                                      Handle<AbstractCode> abstract_code,
                                      int offset);

  void SetDebugBreak();
  void ClearDebugBreak();

  Handle<AbstractCode> abstract_code_;
  int code_offset_;
  DebugBreakType type_;
  int position_;

  friend class CodeBreakIterator;
  friend class BytecodeArrayBreakIterator;
};

class BreakIterator {
 public:
  static std::unique_ptr<BreakIterator> GetIterator(
      Handle<DebugInfo> debug_info, Handle<AbstractCode> abstract_code,
      BreakLocatorType type = ALL_BREAK_LOCATIONS);

  virtual ~BreakIterator() {}

  virtual BreakLocation GetBreakLocation() = 0;
  virtual bool Done() const = 0;
  virtual void Next() = 0;

  void SkipTo(int count) {
    while (count-- > 0) Next();
  }

  virtual int code_offset() = 0;
  int break_index() const { return break_index_; }
  inline int position() const { return position_; }
  inline int statement_position() const { return statement_position_; }

  virtual bool IsDebugBreak() = 0;
  virtual void ClearDebugBreak() = 0;
  virtual void SetDebugBreak() = 0;

 protected:
  explicit BreakIterator(Handle<DebugInfo> debug_info,
                         BreakLocatorType break_locator_type);

  int BreakIndexFromPosition(int position, BreakPositionAlignment alignment);

  Isolate* isolate() { return debug_info_->GetIsolate(); }

  Handle<DebugInfo> debug_info_;
  int break_index_;
  int position_;
  int statement_position_;
  BreakLocatorType break_locator_type_;

 private:
  DisallowHeapAllocation no_gc_;
  DISALLOW_COPY_AND_ASSIGN(BreakIterator);
};

class CodeBreakIterator : public BreakIterator {
 public:
  CodeBreakIterator(Handle<DebugInfo> debug_info, BreakLocatorType type);
  ~CodeBreakIterator() override {}

  BreakLocation GetBreakLocation() override;
  bool Done() const override { return reloc_iterator_.done(); }
  void Next() override;

  bool IsDebugBreak() override;
  void ClearDebugBreak() override;
  void SetDebugBreak() override;

  void SkipToPosition(int position, BreakPositionAlignment alignment);

  int code_offset() override {
    return static_cast<int>(rinfo()->pc() -
                            debug_info_->DebugCode()->instruction_start());
  }

 private:
  int GetModeMask(BreakLocatorType type);
  DebugBreakType GetDebugBreakType();

  RelocInfo::Mode rmode() { return reloc_iterator_.rinfo()->rmode(); }
  RelocInfo* rinfo() { return reloc_iterator_.rinfo(); }

  RelocIterator reloc_iterator_;
  SourcePositionTableIterator source_position_iterator_;
  DISALLOW_COPY_AND_ASSIGN(CodeBreakIterator);
};

class BytecodeArrayBreakIterator : public BreakIterator {
 public:
  BytecodeArrayBreakIterator(Handle<DebugInfo> debug_info,
                             BreakLocatorType type);
  ~BytecodeArrayBreakIterator() override {}

  BreakLocation GetBreakLocation() override;
  bool Done() const override { return source_position_iterator_.done(); }
  void Next() override;

  bool IsDebugBreak() override;
  void ClearDebugBreak() override;
  void SetDebugBreak() override;

  void SkipToPosition(int position, BreakPositionAlignment alignment);

  int code_offset() override { return source_position_iterator_.code_offset(); }

 private:
  DebugBreakType GetDebugBreakType();

  SourcePositionTableIterator source_position_iterator_;
  DISALLOW_COPY_AND_ASSIGN(BytecodeArrayBreakIterator);
};

// Linked list holding debug info objects. The debug info objects are kept as
// weak handles to avoid a debug info object to keep a function alive.
class DebugInfoListNode {
 public:
  explicit DebugInfoListNode(DebugInfo* debug_info);
  ~DebugInfoListNode();

  DebugInfoListNode* next() { return next_; }
  void set_next(DebugInfoListNode* next) { next_ = next; }
  Handle<DebugInfo> debug_info() { return Handle<DebugInfo>(debug_info_); }

 private:
  // Global (weak) handle to the debug info object.
  DebugInfo** debug_info_;

  // Next pointer for linked list.
  DebugInfoListNode* next_;
};

// Details of the debug event delivered to the debug event listener.
class EventDetailsImpl : public v8::Debug::EventDetails {
 public:
  EventDetailsImpl(DebugEvent event, Handle<JSObject> exec_state,
                   Handle<JSObject> event_data, Handle<Object> callback_data);
  virtual DebugEvent GetEvent() const;
  virtual v8::Local<v8::Object> GetExecutionState() const;
  virtual v8::Local<v8::Object> GetEventData() const;
  virtual v8::Local<v8::Context> GetEventContext() const;
  virtual v8::Local<v8::Value> GetCallbackData() const;
  virtual v8::Debug::ClientData* GetClientData() const { return nullptr; }
  virtual v8::Isolate* GetIsolate() const;

 private:
  DebugEvent event_;  // Debug event causing the break.
  Handle<JSObject> exec_state_;         // Current execution state.
  Handle<JSObject> event_data_;         // Data associated with the event.
  Handle<Object> callback_data_;        // User data passed with the callback
                                        // when it was registered.
};


class DebugFeatureTracker {
 public:
  enum Feature {
    kActive = 1,
    kBreakPoint = 2,
    kStepping = 3,
    kHeapSnapshot = 4,
    kAllocationTracking = 5,
    kProfiler = 6,
    kLiveEdit = 7,
  };

  explicit DebugFeatureTracker(Isolate* isolate)
      : isolate_(isolate), bitfield_(0) {}
  void Track(Feature feature);

 private:
  Isolate* isolate_;
  uint32_t bitfield_;
};


// This class contains the debugger support. The main purpose is to handle
// setting break points in the code.
//
// This class controls the debug info for all functions which currently have
// active breakpoints in them. This debug info is held in the heap root object
// debug_info which is a FixedArray. Each entry in this list is of class
// DebugInfo.
class Debug {
 public:
  // Debug event triggers.
  void OnDebugBreak(Handle<Object> break_points_hit);

  void OnThrow(Handle<Object> exception);
  void OnPromiseReject(Handle<Object> promise, Handle<Object> value);
  void OnCompileError(Handle<Script> script);
  void OnAfterCompile(Handle<Script> script);
  void OnAsyncTaskEvent(debug::PromiseDebugActionType type, int id);

  // API facing.
  void SetEventListener(Handle<Object> callback, Handle<Object> data);
  MUST_USE_RESULT MaybeHandle<Object> Call(Handle<Object> fun,
                                           Handle<Object> data);
  Handle<Context> GetDebugContext();
  void HandleDebugBreak();

  // Internal logic
  bool Load();
  void Break(JavaScriptFrame* frame);

  // Scripts handling.
  Handle<FixedArray> GetLoadedScripts();

  // Break point handling.
  bool SetBreakPoint(Handle<JSFunction> function,
                     Handle<Object> break_point_object,
                     int* source_position);
  bool SetBreakPointForScript(Handle<Script> script,
                              Handle<Object> break_point_object,
                              int* source_position,
                              BreakPositionAlignment alignment);
  void ClearBreakPoint(Handle<Object> break_point_object);
  void ChangeBreakOnException(ExceptionBreakType type, bool enable);
  bool IsBreakOnException(ExceptionBreakType type);

  // The parameter is either a BreakPointInfo object, or a FixedArray of
  // BreakPointInfo objects.
  // Returns an empty handle if no breakpoint is hit, or a FixedArray with all
  // hit breakpoints.
  MaybeHandle<FixedArray> GetHitBreakPointObjects(
      Handle<Object> break_point_objects);

  // Stepping handling.
  void PrepareStep(StepAction step_action);
  void PrepareStepIn(Handle<JSFunction> function);
  void PrepareStepInSuspendedGenerator();
  void PrepareStepOnThrow();
  void ClearStepping();
  void ClearStepOut();

  bool PrepareFunctionForBreakPoints(Handle<SharedFunctionInfo> shared);
  bool GetPossibleBreakpoints(Handle<Script> script, int start_position,
                              int end_position, std::set<int>* positions);

  void RecordGenerator(Handle<JSGeneratorObject> generator_object);

  void RunPromiseHook(PromiseHookType type, Handle<JSPromise> promise,
                      Handle<Object> parent);

  int NextAsyncTaskId(Handle<JSObject> promise);

  bool IsBlackboxed(Handle<SharedFunctionInfo> shared);

  void SetDebugDelegate(debug::DebugDelegate* delegate);

  // Returns whether the operation succeeded. Compilation can only be triggered
  // if a valid closure is passed as the second argument, otherwise the shared
  // function needs to be compiled already.
  bool EnsureDebugInfo(Handle<SharedFunctionInfo> shared,
                       Handle<JSFunction> function);
  void CreateDebugInfo(Handle<SharedFunctionInfo> shared);
  static Handle<DebugInfo> GetDebugInfo(Handle<SharedFunctionInfo> shared);

  template <typename C>
  bool CompileToRevealInnerFunctions(C* compilable);

  // This function is used in FunctionNameUsing* tests.
  Handle<Object> FindSharedFunctionInfoInScript(Handle<Script> script,
                                                int position);

  static Handle<Object> GetSourceBreakLocations(
      Handle<SharedFunctionInfo> shared,
      BreakPositionAlignment position_aligment);

  // Check whether a global object is the debug global object.
  bool IsDebugGlobal(JSGlobalObject* global);

  // Check whether this frame is just about to return.
  bool IsBreakAtReturn(JavaScriptFrame* frame);

  // Support for LiveEdit
  void ScheduleFrameRestart(StackFrame* frame);

  bool IsFrameBlackboxed(JavaScriptFrame* frame);

  // Threading support.
  char* ArchiveDebug(char* to);
  char* RestoreDebug(char* from);
  static int ArchiveSpacePerThread();
  void FreeThreadResources() { }
  void Iterate(ObjectVisitor* v);

  bool CheckExecutionState(int id) {
    return CheckExecutionState() && break_id() == id;
  }

  bool CheckExecutionState() {
    return is_active() && !debug_context().is_null() && break_id() != 0;
  }

  bool PerformSideEffectCheck(Handle<JSFunction> function);
  bool PerformSideEffectCheckForCallback(Address function);

  // Flags and states.
  DebugScope* debugger_entry() {
    return reinterpret_cast<DebugScope*>(
        base::NoBarrier_Load(&thread_local_.current_debug_scope_));
  }
  inline Handle<Context> debug_context() { return debug_context_; }

  void set_live_edit_enabled(bool v) { live_edit_enabled_ = v; }
  bool live_edit_enabled() const {
    return FLAG_enable_liveedit && live_edit_enabled_;
  }

  inline bool is_active() const { return is_active_; }
  inline bool is_loaded() const { return !debug_context_.is_null(); }
  inline bool in_debug_scope() const {
    return !!base::NoBarrier_Load(&thread_local_.current_debug_scope_);
  }
  void set_break_points_active(bool v) { break_points_active_ = v; }
  bool break_points_active() const { return break_points_active_; }

  StackFrame::Id break_frame_id() { return thread_local_.break_frame_id_; }
  int break_id() { return thread_local_.break_id_; }

  Object* return_value() { return thread_local_.return_value_; }
  void set_return_value(Object* value) { thread_local_.return_value_ = value; }

  // Support for embedding into generated code.
  Address is_active_address() {
    return reinterpret_cast<Address>(&is_active_);
  }

  Address hook_on_function_call_address() {
    return reinterpret_cast<Address>(&hook_on_function_call_);
  }

  Address last_step_action_address() {
    return reinterpret_cast<Address>(&thread_local_.last_step_action_);
  }

  Address suspended_generator_address() {
    return reinterpret_cast<Address>(&thread_local_.suspended_generator_);
  }

  Address restart_fp_address() {
    return reinterpret_cast<Address>(&thread_local_.restart_fp_);
  }

  StepAction last_step_action() { return thread_local_.last_step_action_; }

  DebugFeatureTracker* feature_tracker() { return &feature_tracker_; }

 private:
  explicit Debug(Isolate* isolate);

  void UpdateState();
  void UpdateHookOnFunctionCall();
  void Unload();
  void SetNextBreakId() {
    thread_local_.break_id_ = ++thread_local_.break_count_;
  }

  inline bool ignore_events() const {
    return is_suppressed_ || !is_active_ || isolate_->needs_side_effect_check();
  }
  inline bool break_disabled() const {
    return break_disabled_ || in_debug_event_listener_;
  }

  void clear_suspended_generator() {
    thread_local_.suspended_generator_ = Smi::kZero;
  }

  bool has_suspended_generator() const {
    return thread_local_.suspended_generator_ != Smi::kZero;
  }

  // There are three types of event listeners: C++ message_handler,
  // JavaScript event listener and C++ event listener.
  // Currently inspector still uses C++ event listener and installs
  // more specific event listeners for part of events. Calling of
  // C++ event listener is redundant when more specific event listener
  // is presented. Other clients can install JavaScript event listener
  // (e.g. some of NodeJS module).
  bool non_inspector_listener_exists() const {
    return !event_listener_.is_null() && !event_listener_->IsForeign();
  }

  bool IsBlackboxed(SharedFunctionInfo* shared);
  bool IsExceptionBlackboxed(bool uncaught);

  void OnException(Handle<Object> exception, Handle<Object> promise);

  // Constructors for debug event objects.
  MUST_USE_RESULT MaybeHandle<Object> MakeExecutionState();
  MUST_USE_RESULT MaybeHandle<Object> MakeBreakEvent(
      Handle<Object> break_points_hit);
  MUST_USE_RESULT MaybeHandle<Object> MakeExceptionEvent(
      Handle<Object> exception,
      bool uncaught,
      Handle<Object> promise);
  MUST_USE_RESULT MaybeHandle<Object> MakeCompileEvent(
      Handle<Script> script, v8::DebugEvent type);
  MUST_USE_RESULT MaybeHandle<Object> MakeAsyncTaskEvent(Handle<Smi> type,
                                                         Handle<Smi> id);

  // Mirror cache handling.
  void ClearMirrorCache();

  void ProcessCompileEvent(v8::DebugEvent event, Handle<Script> script);
  void ProcessDebugEvent(v8::DebugEvent event, Handle<JSObject> event_data);

  // Find the closest source position for a break point for a given position.
  int FindBreakablePosition(Handle<DebugInfo> debug_info, int source_position,
                            BreakPositionAlignment alignment);
  // Instrument code to break at break points.
  void ApplyBreakPoints(Handle<DebugInfo> debug_info);
  // Clear code from instrumentation.
  void ClearBreakPoints(Handle<DebugInfo> debug_info);
  // Clear all code from instrumentation.
  void ClearAllBreakPoints();
  // Instrument a function with one-shots.
  void FloodWithOneShot(Handle<JSFunction> function,
                        BreakLocatorType type = ALL_BREAK_LOCATIONS);
  // Clear all one-shot instrumentations, but restore break points.
  void ClearOneShot();

  void ActivateStepOut(StackFrame* frame);
  void RemoveDebugInfoAndClearFromShared(Handle<DebugInfo> debug_info);
  MaybeHandle<FixedArray> CheckBreakPoints(Handle<DebugInfo> debug_info,
                                           BreakLocation* location,
                                           bool* has_break_points = nullptr);
  bool IsMutedAtCurrentLocation(JavaScriptFrame* frame);
  bool CheckBreakPoint(Handle<Object> break_point_object);
  MaybeHandle<Object> CallFunction(const char* name, int argc,
                                   Handle<Object> args[]);

  inline void AssertDebugContext() {
    DCHECK(isolate_->context() == *debug_context());
    DCHECK(in_debug_scope());
  }

  void ThreadInit();

  void PrintBreakLocation();

  // Global handles.
  Handle<Context> debug_context_;
  Handle<Object> event_listener_;
  Handle<Object> event_listener_data_;

  debug::DebugDelegate* debug_delegate_ = nullptr;

  // Debugger is active, i.e. there is a debug event listener attached.
  bool is_active_;
  // Debugger needs to be notified on every new function call.
  // Used for stepping and read-only checks
  bool hook_on_function_call_;
  // Suppress debug events.
  bool is_suppressed_;
  // LiveEdit is enabled.
  bool live_edit_enabled_;
  // Do not trigger debug break events.
  bool break_disabled_;
  // Do not break on break points.
  bool break_points_active_;
  // Nested inside a debug event listener.
  bool in_debug_event_listener_;
  // Trigger debug break events for all exceptions.
  bool break_on_exception_;
  // Trigger debug break events for uncaught exceptions.
  bool break_on_uncaught_exception_;
  // Termination exception because side effect check has failed.
  bool side_effect_check_failed_;

  // List of active debug info objects.
  DebugInfoListNode* debug_info_list_;

  // Used to collect histogram data on debugger feature usage.
  DebugFeatureTracker feature_tracker_;

  // Per-thread data.
  class ThreadLocal {
   public:
    // Top debugger entry.
    base::AtomicWord current_debug_scope_;

    // Counter for generating next break id.
    int break_count_;

    // Current break id.
    int break_id_;

    // Frame id for the frame of the current break.
    StackFrame::Id break_frame_id_;

    // Step action for last step performed.
    StepAction last_step_action_;

    // Source statement position from last step next action.
    int last_statement_position_;

    // Frame pointer from last step next or step frame action.
    Address last_fp_;

    // Frame pointer of the target frame we want to arrive at.
    Address target_fp_;

    // Value of the accumulator at the point of entering the debugger.
    Object* return_value_;

    // The suspended generator object to track when stepping.
    Object* suspended_generator_;

    // The new frame pointer to drop to when restarting a frame.
    Address restart_fp_;

    int async_task_count_;
  };

  // Storage location for registers when handling debug break calls
  ThreadLocal thread_local_;

  Isolate* isolate_;

  friend class Isolate;
  friend class DebugScope;
  friend class DisableBreak;
  friend class LiveEdit;
  friend class SuppressDebug;
  friend class NoSideEffectScope;

  friend Handle<FixedArray> GetDebuggedFunctions();  // In test-debug.cc
  friend void CheckDebuggerUnloaded(bool check_functions);  // In test-debug.cc

  DISALLOW_COPY_AND_ASSIGN(Debug);
};


// This scope is used to load and enter the debug context and create a new
// break state.  Leaving the scope will restore the previous state.
// On failure to load, FailedToEnter returns true.
class DebugScope BASE_EMBEDDED {
 public:
  explicit DebugScope(Debug* debug);
  ~DebugScope();

  // Check whether loading was successful.
  inline bool failed() { return failed_; }

  // Get the active context from before entering the debugger.
  inline Handle<Context> GetContext() { return save_.context(); }

 private:
  Isolate* isolate() { return debug_->isolate_; }

  Debug* debug_;
  DebugScope* prev_;               // Previous scope if entered recursively.
  StackFrame::Id break_frame_id_;  // Previous break frame id.
  int break_id_;                   // Previous break id.
  Handle<Object> return_value_;    // Previous result.
  bool failed_;                    // Did the debug context fail to load?
  SaveContext save_;               // Saves previous context.
  PostponeInterruptsScope no_termination_exceptons_;
};


// Stack allocated class for disabling break.
class DisableBreak BASE_EMBEDDED {
 public:
  explicit DisableBreak(Debug* debug)
      : debug_(debug),
        previous_break_disabled_(debug->break_disabled_),
        previous_in_debug_event_listener_(debug->in_debug_event_listener_) {
    debug_->break_disabled_ = true;
    debug_->in_debug_event_listener_ = true;
  }
  ~DisableBreak() {
    debug_->break_disabled_ = previous_break_disabled_;
    debug_->in_debug_event_listener_ = previous_in_debug_event_listener_;
  }

 private:
  Debug* debug_;
  bool previous_break_disabled_;
  bool previous_in_debug_event_listener_;
  DISALLOW_COPY_AND_ASSIGN(DisableBreak);
};


class SuppressDebug BASE_EMBEDDED {
 public:
  explicit SuppressDebug(Debug* debug)
      : debug_(debug), old_state_(debug->is_suppressed_) {
    debug_->is_suppressed_ = true;
  }
  ~SuppressDebug() { debug_->is_suppressed_ = old_state_; }

 private:
  Debug* debug_;
  bool old_state_;
  DISALLOW_COPY_AND_ASSIGN(SuppressDebug);
};

class NoSideEffectScope {
 public:
  NoSideEffectScope(Isolate* isolate, bool disallow_side_effects)
      : isolate_(isolate),
        old_needs_side_effect_check_(isolate->needs_side_effect_check()) {
    isolate->set_needs_side_effect_check(old_needs_side_effect_check_ ||
                                         disallow_side_effects);
    isolate->debug()->UpdateHookOnFunctionCall();
    isolate->debug()->side_effect_check_failed_ = false;
  }
  ~NoSideEffectScope();

 private:
  Isolate* isolate_;
  bool old_needs_side_effect_check_;
  DISALLOW_COPY_AND_ASSIGN(NoSideEffectScope);
};

// Code generator routines.
class DebugCodegen : public AllStatic {
 public:
  enum DebugBreakCallHelperMode {
    SAVE_RESULT_REGISTER,
    IGNORE_RESULT_REGISTER
  };

  static void GenerateDebugBreakStub(MacroAssembler* masm,
                                     DebugBreakCallHelperMode mode);

  static void GenerateSlot(MacroAssembler* masm, RelocInfo::Mode mode);

  // Builtin to drop frames to restart function.
  static void GenerateFrameDropperTrampoline(MacroAssembler* masm);

  // Builtin to atomically (wrt deopts) handle debugger statement and
  // drop frames to restart function if necessary.
  static void GenerateHandleDebuggerStatement(MacroAssembler* masm);

  static void PatchDebugBreakSlot(Isolate* isolate, Address pc,
                                  Handle<Code> code);
  static bool DebugBreakSlotIsPatched(Address pc);
  static void ClearDebugBreakSlot(Isolate* isolate, Address pc);
};


}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_H_
