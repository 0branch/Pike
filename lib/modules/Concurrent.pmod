#pike __REAL_VERSION__

//! Module for handling multiple concurrent events.
//!
//! The @[Future] and @[Promise] API was inspired by
//! @url{https://github.com/couchdeveloper/FutureLib@}.

protected enum State {
  STATE_PENDING = 0,
  STATE_FULFILLED,
  STATE_REJECTED,
};

protected Thread.Mutex mux = Thread.Mutex();
protected Thread.Condition cond = Thread.Condition();

//! Global failure callback, called when a promise without failure
//! callback fails. This is useful to log exceptions, so they are not
//! just silently caught and ignored.
void on_failure(function(mixed : void) f)
{
  global_on_failure = f;
}
protected function(mixed : void) global_on_failure;

//! Value that will be provided asynchronously
//! sometime in the future.
//!
//! @seealso
//!   @[Promise]
class Future
{
  mixed result = UNDEFINED;
  State state;

  protected array(array(function(mixed, mixed ...: void)|array(mixed)))
    success_cbs = ({});
  protected array(array(function(mixed, mixed ...: void)|array(mixed)))
    failure_cbs = ({});

  //! Wait for fulfillment and return the value.
  //!
  //! @throws
  //!   Throws on rejection.
  mixed get()
  {
    State s = state;
    mixed res = result;
    if (!s) {
      object key = mux->lock();
      while (!state) {
	cond->wait(key);
      }

      s = state;
      res = result;
      key = 0;
    }

    if (s == STATE_REJECTED) {
      throw(res);
    }
    return res;
  }

  //! Register a callback that is to be called on fulfillment.
  //!
  //! @param cb
  //!   Function to be called. The first argument will be the
  //!   result of the @[Future].
  //!
  //! @param extra
  //!   Any extra context needed for @[cb]. They will be provided
  //!   as arguments two and onwards when @[cb] is called.
  //!
  //! @note
  //!   @[cb] will always be called from the main backend.
  //!
  //! @seealso
  //!   @[on_failure()]
  this_program on_success(function(mixed, mixed ... : void) cb, mixed ... extra)
  {
    object key = mux->lock();

    if (state == STATE_FULFILLED) {
      call_out(cb, 0, result, @extra);
      key = 0;
      return this_program::this;
    }

    success_cbs += ({ ({ cb, extra }) });
    key = 0;

    return this_program::this;
  }

  //! Register a callback that is to be called on failure.
  //!
  //! @param cb
  //!   Function to be called. The first argument will be the
  //!   failure result of the @[Future].
  //!
  //! @param extra
  //!   Any extra context needed for @[cb]. They will be provided
  //!   as arguments two and onwards when @[cb] is called.
  //!
  //! @note
  //!   @[cb] will always be called from the main backend.
  //!
  //! @seealso
  //!   @[on_success()]
  this_program on_failure(function(mixed, mixed ... : void) cb, mixed ... extra)
  {
    object key = mux->lock();

    if (state == STATE_REJECTED) {
      call_out(cb, 0, result, @extra);
      key = 0;
      return this_program::this;
    }

    failure_cbs += ({ ({ cb, extra }) });
    key = 0;

    return this_program::this;
  }

  //! Apply @[fun] with @[val] followed by the contents of @[ctx],
  //! and update @[p] with the result.
  protected void apply(mixed val, Promise p,
		       function(mixed, mixed ... : mixed) fun,
		       array(mixed) ctx)
  {
    mixed err = catch {
	p->success(fun(val, @ctx));
	return;
      };
    p->failure(err);
  }

  //! Apply @[fun] with @[val] followed by the contents of @[ctx],
  //! and update @[p] with the eventual result.
  protected void apply_flat(mixed val, Promise p,
			    function(mixed, mixed ... : Future) fun,
			    array(mixed) ctx)
  {
    mixed err = catch {
	Future f = fun(val, @ctx);
	if (!objectp(f) || !f->on_failure || !f->on_success) {
	  error("Expected %O to return a Future. Got: %O.\n",
		fun, f);
	}
	f->on_failure(p->failure);
	f->on_success(p->success);
	return;
      };
    p->failure(err);
  }

  //! Apply @[fun] with @[val] followed by the contents of @[ctx],
  //! and update @[p] with the eventual result.
  protected void apply_smart(mixed val, Promise p,
			    function(mixed, mixed ... : mixed|Future) fun,
			    array(mixed) ctx)
  {
    mixed err = catch {
	mixed|Future f = fun(val, @ctx);
	if (!objectp(f)
         || !functionp(f->on_failure) || !functionp(f->on_success)) {
	  p->success(f);
	  return;
	}
	f->on_failure(p->failure);
	f->on_success(p->success);
	return;
      };
    p->failure(err);
  }

  //! Apply @[fun] with @[val] followed by the contents of @[ctx],
  //! and update @[p] with @[val] if @[fun] didn't return false.
  //! If @[fun] returned false fail @[p] with @[UNDEFINED].
  protected void apply_filter(mixed val, Promise p,
			      function(mixed, mixed ... : int(0..1)) fun,
			      array(mixed) ctx)
  {
    mixed err = catch {
	if (fun(val, @ctx)) {
	  p->success(val);
	} else {
	  p->failure(UNDEFINED);
	}
	return;
      };
    p->failure(err);
  }

  //! Return a @[Future] that will be fulfilled with the result
  //! of applying @[fun] with the fulfilled result of this @[Future]
  //! followed by @[extra].
  //!
  //! @note
  //!  This method is used if your @[fun] returns a regular value (i.e.
  //!   @b{not@} a @[Future]).
  //!
  //! @seealso
  //!  @[map_with()], @[transform()], @[recover()]
  this_program map(function(mixed, mixed ... : mixed) fun, mixed ... extra)
  {
    Promise p = Promise();
    on_failure(p->failure);
    on_success(apply, p, fun, extra);
    return p->future();
  }

  //! Return a @[Future] that will be fulfilled with the fulfilled result
  //! of applying @[fun] with the fulfilled result of this @[Future]
  //! followed by @[extra].
  //!
  //! @note
  //!  This method is used if your @[fun] returns a @[Future] again.
  //!
  //! @seealso
  //!  @[map()], @[transform_with()], @[recover_with()], @[flat_map]
  this_program map_with(function(mixed, mixed ... : this_program) fun,
			mixed ... extra)
  {
    Promise p = Promise();
    on_failure(p->failure);
    on_success(apply_flat, p, fun, extra);
    return p->future();
  }

  //! This is an alias for @[map_with()].
  //!
  //! @seealso
  //!   @[map_with()]
  inline this_program flat_map(function(mixed, mixed ... : this_program) fun,
			       mixed ... extra)
  {
    return map_with(fun, @extra);
  }

  //! Return a @[Future] that will be fulfilled with either
  //! the fulfilled result of this @[Future], or the result
  //! of applying @[fun] with the failed result followed
  //! by @[extra].
  //!
  //! @note
  //!  This method is used if your callbacks returns a regular value (i.e.
  //!   @b{not@} a @[Future]).
  //!
  //! @seealso
  //!   @[recover_with()], @[map()], @[transform()]
  this_program recover(function(mixed, mixed ... : mixed) fun,
		       mixed ... extra)
  {
    Promise p = Promise();
    on_success(p->success);
    on_failure(apply, p, fun, extra);
    return p->future();
  }

  //! Return a @[Future] that will be fulfilled with either
  //! the fulfilled result of this @[Future], or the fulfilled result
  //! of applying @[fun] with the failed result followed
  //! by @[extra].
  //!
  //! @note
  //!  This method is used if your callbacks returns a @[Future] again.
  //!
  //! @seealso
  //!   @[recover()], @[map_with()], @[transform_with()]
  this_program recover_with(function(mixed, mixed ... : this_program) fun,
			    mixed ... extra)
  {
    Promise p = Promise();
    on_success(p->success);
    on_failure(apply_flat, p, fun, extra);
    return p->future();
  }

  //! Return a @[Future] that either will by fulfilled by the
  //! fulfilled result of this @[Future] if applying @[fun]
  //! with the result followed by @[extra] returns true,
  //! or will fail with @[UNDEFINED] if it returns false.
  this_program filter(function(mixed, mixed ... : int(0..1)) fun,
		      mixed ... extra)
  {
    Promise p = Promise();
    on_failure(p->failure);
    on_success(apply_filter, p, fun, extra);
    return p->future();
  }

  //! Return a @[Future] that will be fulfilled with either
  //! the result of applying @[success] with the fulfilled result
  //! followed by @[extra], or the result of applying @[failure]
  //! with the failed result followed by @[extra].
  //!
  //! @[failure] defaults to @[success].
  //!
  //! @note
  //!  This method is used if your callbacks returns a regular value (i.e.
  //!   @b{not@} a @[Future]).
  //!
  //! @seealso
  //!   @[transform_with()], @[map()], @[recover()]
  this_program transform(function(mixed, mixed ... : mixed) success,
			 function(mixed, mixed ... : mixed)|void failure,
			 mixed ... extra)
  {
    Promise p = Promise();
    on_success(apply, p, success, extra);
    on_failure(apply, p, failure || success, extra);
    return p->future();
  }

  //! Return a @[Future] that will be fulfilled with either
  //! the fulfilled result of applying @[success] with the fulfilled result
  //! followed by @[extra], or the fulfilled result of applying @[failure]
  //! with the failed result followed by @[extra].
  //!
  //! @[failure] defaults to @[success].
  //!
  //! @note
  //!  This method is used if your callbacks returns a @[Future] again.
  //!
  //! @seealso
  //!   @[transform()], @[map_with()], @[recover_with]
  this_program transform_with(function(mixed, mixed ... : this_program) success,
		         function(mixed, mixed ... : this_program)|void failure,
			      mixed ... extra)
  {
    Promise p = Promise();
    on_success(apply_flat, p, success, extra);
    on_failure(apply_flat, p, failure || success, extra);
    return p->future();
  }

  //! @returns
  //! A @[Future] that will be fulfilled with an
  //! array of the fulfilled result of this object followed
  //! by the fulfilled results of @[others].
  //!
  //! @seealso
  //!   @[results()]
  this_program zip(this_program ... others)
  {
    if (!sizeof(others)) return this_program::this;
    return results(({ this_program::this }) + others);
  }

  //! JavaScript Promise API close but not identical equivalent
  //! of a combined @[transform()] and @[transform_with()].
  //!
  //! @param onfulfilled
  //!   Function to be called on fulfillment. The first argument will be the
  //!   result of @b{this@} @[Future].
  //!   The return value will be the result of the new @[Future].
  //!   If the return value already is a @[Future], pass it as-is.
  //!
  //! @param onrejected
  //!   Function to be called on failure. The first argument will be the
  //!   failure result of @b{this@} @[Future].
  //!   The return value will be the failure result of the new @[Future].
  //!   If the return value already is a @[Future], pass it as-is.
  //!
  //! @param extra
  //!   Any extra context needed for @expr{onfulfilled@} and
  //!   @expr{onrejected@}. They will be provided
  //!   as arguments two and onwards when the callbacks are called.
  //!
  //! @returns
  //! The new @[Future].
  //!
  //! @seealso
  //!   @[transform()], @[transform_with()], @[thencatch()],
  //!   @[on_success()], @[Promise.success()],
  //!   @[on_failure()], @[Promise.failure()],
  //!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
  this_program then(void|function(mixed, mixed ... : mixed) onfulfilled,
   void|function(mixed, mixed ... : mixed) onrejected,
   mixed ... extra) {
    Promise p = Promise();
    if (onfulfilled)
      on_success(apply_smart, p, onfulfilled, extra);
    else
      on_success(p->success);
    if (onrejected)
      on_failure(apply_smart, p, onrejected, extra);
    else
      on_failure(p->failure);
    return p->future();
  }

  //! JavaScript Promise API equivalent of a combination of @[recover()]
  //! and @[recover_with()].
  //!
  //! @param onrejected
  //!   Function to be called. The first argument will be the
  //!   failure result of @b{this@} @[Future].
  //!   The return value will the failure result of the new @[Future].
  //!   If the return value already is a @[Future], pass it as-is.
  //!
  //! @param extra
  //!   Any extra context needed for
  //!   @expr{onrejected@}. They will be provided
  //!   as arguments two and onwards when the callback is called.
  //!
  //! @returns
  //! The new @[Future].
  //!
  //! @seealso
  //!   @[recover()], @[recover_with()], @[then()], @[on_failure()],
  //!   @[Promise.failure()],
  //!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
  inline this_program thencatch(function(mixed, mixed ... : mixed) onrejected,
   mixed ... extra) {
    return then(0, onrejected, @extra);
  }

  //! Return a @[Future] that will either be fulfilled with the fulfilled
  //! result of this @[Future], or be failed after @[seconds] have expired.
  this_program timeout(int|float seconds)
  {
    Promise p = Promise();
    on_failure(p->failure);
    on_success(p->success);
    call_out(p->try_failure, seconds, ({ "Timeout.\n", backtrace() }));
    return p->future();
  }

  protected string _sprintf(int t)
  {
    return t=='O' && sprintf("%O(%s,%O)", this_program,
                             ([ STATE_PENDING : "pending",
                                STATE_REJECTED : "rejected",
                                STATE_FULFILLED : "fulfilled" ])[state],
                             result);
  }
}

class aggregate_state {
  private Promise promise;
  private int(0..) promises;
  private int(0..) succeeded, failed;
  final array(mixed) results;
  final int(0..) min_failures;
  final int(-1..) max_failures;
  final mixed accumulator;
  final function(mixed, mixed, mixed ... : mixed) fold_fun;
  final array(mixed) extra;

  private void create(Promise p) {
    if (p->_materialised || p->_materialised++)
      error("Cannot materialise a Promise more than once.\n");
    promise = p;
  }

  final void materialise() {
    Thread.MutexKey key = mux->lock();
    if (promise->_astate) {
      promise->_astate = 0;
      key = 0;
      if (results) {
        promises = sizeof(results);
        array(Future) futures = results;
        if (fold_fun)
          results = 0;
        foreach(futures; int idx; Future f) {
          f->on_failure(cb_failure, idx);
          f->on_success(cb_success, idx);
        }
      }
    }
    key = 0;
  }

  private void fold_one(mixed val) {
    mixed err = catch (accumulator = fold_fun(val, accumulator, @extra));
    if (err) {
      Promise p = promise;		// Cache it, to cover a failure race
      if (p) p->failure(err);
    }
  }

  private void fold(function(mixed:void) failsucc) {
    failsucc(fold_fun ? accumulator : results);
    results = 0;			// Free memory
  }

  private void cb_failure(mixed value, int idx) {
    Promise p;				// Cache it, to cover a failure race
    if (p = promise) {
      object key = mux->lock();
      do {
        if (!p->state) {
          ++failed;
          if (max_failures < failed && max_failures >= 0) {
            key = 0;
            p->try_failure(value);
            break;
          }
          int success = succeeded + failed == promises;
          key = 0;
          if (results)
            results[idx] = value;
          else
            fold_one(value);
          if (success) {
            fold(failed >= min_failures ? p->success : p->failure);
            break;
          }
        } else
          key = 0;
        return;
      } while (0);
      promise = 0;			// Free backreference
    }
  }

  private void cb_success(mixed value, int idx) {
    Promise p;				// Cache it, to cover a failure race
    if (p = promise) {
      object key = mux->lock();
      do {
        if (!p->state) {
          ++succeeded;
          if (promises - min_failures < succeeded) {
            key = 0;
            p->try_failure(value);
            break;
          }
          int success = succeeded + failed == promises;
          key = 0;
          if (results)
            results[idx] = value;
          else
            fold_one(value);
          if (success) {
            fold(p->success);
            break;
          }
        } else
          key = 0;
        return;
      } while (0);
      promise = 0;			// Free backreference
    }
  }
}

//! Promise to provide a @[Future] value.
//!
//! Objects of this class are typically kept internal to the
//! code that provides the @[Future] value. The only thing
//! that is directly returned to the user is the return
//! value from @[future()].
//!
//! @seealso
//!   @[Future], @[future()]
class Promise
{
  inherit Future;

  final int _materialised;
  final aggregate_state _astate;

  //! Creates a new promise, optionally initialised from a traditional callback
  //! driven method via @expr{executor(resolve, reject, extra ... )@}.
  //!
  //! @seealso
  //!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
  protected void create(void|
   function(function(mixed:void),
            function(mixed:void), mixed ...:void) executor, mixed ... extra) {
    if (executor)
      executor(success, failure, @extra);
  }

  Future on_success(function(mixed, mixed ... : void) cb, mixed ... extra) {
    if (_astate) catch(_astate->materialise());	// catches race for _astate == 0
    return ::on_success(cb, @extra);
  }

  Future on_failure(function(mixed, mixed ... : void) cb, mixed ... extra) {
    if (_astate) catch(_astate->materialise());	// catches race for _astate == 0
    return ::on_failure(cb, @extra);
  }

  mixed get() {
    if (_astate) catch(_astate->materialise());	// catches race for _astate == 0
    return ::get();
  }

  //! The future value that we promise.
  Future future()
  {
    return Future::this;
  }

  protected void unlocked_success(mixed value)
  {
    if (state < STATE_REJECTED) {
      result = value;
      state = STATE_FULFILLED;
      cond->broadcast();
      foreach(success_cbs,
	      [function(mixed, mixed ...: void) cb,
	       array(mixed) extra]) {
	if (cb) {
	  call_out(cb, 0, value, @extra);
	}
      }
    }
  }

  //! Fulfill the @[Future].
  //!
  //! @param value
  //!   Result of the @[Future].
  //!
  //! @throws
  //!   Throws an error if the @[Future] already has been fulfilled
  //!   or failed.
  //!
  //! Mark the @[Future] as fulfilled, and schedule the @[on_success()]
  //! callbacks to be called as soon as possible.
  //!
  //! @seealso
  //!   @[try_success()], @[try_failure()], @[failure()], @[on_success()]
  void success(mixed value)
  {
    if (state) error("Promise has already been finalised.\n");
    object key = mux->lock();
    if (state) error("Promise has already been finalised.\n");
    unlocked_success(value);
    key = 0;
  }

  //! Fulfill the @[Future] if it hasn't been fulfilled or failed already.
  //!
  //! @param value
  //!   Result of the @[Future].
  //!
  //! Mark the @[Future] as fulfilled if it hasn't already been fulfilled
  //! or failed, and in that case schedule the @[on_success()] callbacks
  //! to be called as soon as possible.
  //!
  //! @seealso
  //!   @[success()], @[try_failure()], @[failure()], @[on_success()]
  void try_success(mixed value)
  {
    if (state) return;
    object key = mux->lock();
    if (state) return;
    unlocked_success(value);
    key = 0;
  }

  protected void unlocked_failure(mixed value)
  {
    state = STATE_REJECTED;
    result = value;
    cond->broadcast();
    if( !sizeof(failure_cbs) && global_on_failure )
      call_out(global_on_failure, 0, value);
    foreach(failure_cbs,
	    [function(mixed, mixed ...: void) cb,
	     array(mixed) extra]) {
      if (cb) {
	call_out(cb, 0, value, @extra);
      }
    }
  }

  //! Reject the @[Future] value.
  //!
  //! @param value
  //!   Failure result of the @[Future].
  //!
  //! @throws
  //!   Throws an error if the @[Future] already has been fulfilled
  //!   or failed.
  //!
  //! Mark the @[Future] as failed, and schedule the @[on_failure()]
  //! callbacks to be called as soon as possible.
  //!
  //! @seealso
  //!   @[try_failure()], @[success()], @[on_failure()]
  void failure(mixed value)
  {
    if (state) error("Promise has already been finalised.\n");
    object key = mux->lock();
    if (state) error("Promise has already been finalised.\n");
    unlocked_failure(value);
    key = 0;
  }

  //! Maybe reject the @[Future] value.
  //!
  //! @param value
  //!   Failure result of the @[Future].
  //!
  //! Mark the @[Future] as failed if it hasn't already been fulfilled,
  //! and in that case schedule the @[on_failure()] callbacks to be
  //! called as soon as possible.
  //!
  //! @seealso
  //!   @[failure()], @[success()], @[on_failure()]
  void try_failure(mixed value)
  {
    if (state) return;
    object key = mux->lock();
    if (state) return;
    unlocked_failure(value);
  }

  inline private void fill_astate() {
    if (!_astate)
      _astate = aggregate_state(this);
  }

  //! Add futures to the list of futures which the current object depends upon.
  //!
  //! If called without arguments it will produce a new @[Future]
  //! from a new @[Promise] which is implictly added to the dependency list.
  //!
  //! @param futures
  //!   The list of @expr{futures@} we want to add to the list we depend on.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @note
  //!  Can be called multiple times to add more.
  //!
  //! @note
  //!  Once the promise has been materialised (when either @[on_success()],
  //!  @[on_failure()] or @[get()] has been called on this object), it is
  //!  not possible to call @[depend()] anymore.
  //!
  //! @seealso
  //!   @[fold()], @[first_completed()], @[max_failures()], @[min_failures()],
  //!   @[any_results()], @[Concurrent.results()], @[Concurrent.all()]
  this_program depend(array(Future) futures)
  {
    if (sizeof(futures)) {
      fill_astate();
      _astate->results += futures;
    }
    return this_program::this;
  }
  inline variant this_program depend(Future ... futures)
  {
    return depend(futures);
  }
  variant this_program depend()
  {
    Promise p = Promise();
    depend(p->future());
    return p;
  }

  //! @param initial
  //!   Initial value of the accumulator.
  //!
  //! @param fun
  //!   Function to apply. The first argument is the result of
  //!   one of the @[futures].  The second argument is the current value
  //!   of the accumulator.
  //!
  //! @param extra
  //!   Any extra context needed for @[fun]. They will be provided
  //!   as arguments three and onwards when @[fun] is called.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @note
  //!   If @[fun] throws an error it will fail the @[Future].
  //!
  //! @note
  //!   @[fun] may be called in any order, and will be called
  //!   once for every @[Future] it depends upon, unless one of the
  //!   calls fails in which case no further calls will be
  //!   performed.
  //!
  //! @seealso
  //!   @[depend()], @[Concurrent.fold()]
  this_program fold(mixed initial,
	            function(mixed, mixed, mixed ... : mixed) fun,
	            mixed ... extra)
  {
    if (_astate) {
      _astate->accumulator = initial;
      _astate->fold_fun = fun;
      _astate->extra = extra;
      _astate->materialise();
    } else
      success(initial);
    return this_program::this;
  }

  //! It evaluates to the first future that completes of the list
  //! of futures it depends upon.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @seealso
  //!   @[depend()], @[Concurrent.first_completed()]
  this_program first_completed()
  {
    if (_astate) {
      _astate->results->on_failure(try_failure);
      _astate->results->on_success(try_success);
      _astate = 0;
    } else
      success(0);
    return this_program::this;
  }

  //! @param max
  //!   Specifies the maximum number of failures to be accepted in
  //!   the list of futures this promise depends upon.  Defaults
  //!   to @expr{0@}.  @expr{-1@} means unlimited.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @seealso
  //!   @[depend()], @[min_failures()], @[any_results()]
  this_program max_failures(int(-1..) max)
  {
    fill_astate();
    _astate->max_failures = max;
    return this_program::this;
  }

  //! @param min
  //!   Specifies the minimum number of failures to be required in
  //!   the list of futures this promise depends upon.  Defaults
  //!   to @expr{0@}.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @seealso
  //!   @[depend()], @[max_failures()]
  this_program min_failures(int(0..) min)
  {
    fill_astate();
    _astate->min_failures = min;
    return this_program::this;
  }

  //! Sets the number of failures to be accepted in the list of futures
  //! this promise
  //! depends upon to unlimited.  It is equivalent to @expr{max_failures(-1)@}.
  //!
  //! @returns
  //! The new @[Promise].
  //!
  //! @seealso
  //!   @[depend()], @[max_failures()]
  this_program any_results()
  {
    return max_failures(-1);
  }

  protected void _destruct()
  {
    if (!state) {
      unlocked_failure(({ "Promise broken.\n", backtrace() }));
    }
  }
}

//! @returns
//! A @[Future] that represents the first
//! of the @expr{futures@} that completes.
//!
//! @seealso
//!   @[race()], @[Promise.first_completed()]
variant Future first_completed(array(Future) futures)
{
  return Promise()->depend(futures)->first_completed()->future();
}
variant inline Future first_completed(Future ... futures)
{
  return first_completed(futures);
}

//! JavaScript Promise API equivalent of @[first_completed()].
//!
//! @seealso
//!   @[first_completed()], @[Promise.first_completed()]
//!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
variant inline Future race(array(Future) futures)
{
  return first_completed(futures);
}
variant inline Future race(Future ... futures)
{
  return first_completed(futures);
}

//! @returns
//! A @[Future] that represents the array of all the completed @expr{futures@}.
//!
//! @seealso
//!   @[all()], @[Promise.depend()]
variant Future results(array(Future) futures)
{
  Promise p = Promise();
  p->depend(futures);
  return p->future();
}
inline variant Future results(Future ... futures)
{
  return results(futures);
}

//! JavaScript Promise API equivalent of @[results()].
//!
//! @seealso
//!   @[results()], @[Promise.depend()]
//!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
inline variant Future all(array(Future) futures)
{
  return results(futures);
}
inline variant Future all(Future ... futures)
{
  return results(futures);
}

//! @returns
//! A new @[Future] that has already failed for the specified @expr{reason@}.
//!
//! @seealso
//!   @[Future.on_failure()], @[Promise.failure()]
//!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
Future reject(mixed reason)
{
  object p = Promise();
  p->failure(reason);
  return p->future();
}

//! @returns
//! A new @[Future] that has already been fulfilled with @expr{value@}
//! as result.  If @expr{value@} is an object which already
//! has @[on_failure] and @[on_success] methods, return it unchanged.
//!
//! @note
//! This function can be used to ensure values are futures.
//!
//! @seealso
//!   @[Future.on_success()], @[Promise.success()]
//!   @url{https://developer.mozilla.org/en/docs/Web/JavaScript/Reference/Global_Objects/Promise@}
Future resolve(mixed value)
{
  if (objectp(value) && value->on_failure && value->on_success)
    return value;
  object p = Promise();
  p->success(value);
  return p->future();
}

//! Return a @[Future] that represents the array of mapping @[fun]
//! over the results of the completed @[futures].
Future traverse(array(Future) futures,
		function(mixed, mixed ... : mixed) fun,
		mixed ... extra)
{
  return results(futures->map(fun, @extra));
}

//! Return a @[Future] that represents the accumulated results of
//! applying @[fun] to the results of the @[futures] in turn.
//!
//! @param initial
//!   Initial value of the accumulator.
//!
//! @param fun
//!   Function to apply. The first argument is the result of
//!   one of the @[futures], the second the current accumulated
//!   value, and any further from @[extra].
//!
//! @note
//!   If @[fun] throws an error it will fail the @[Future].
//!
//! @note
//!   @[fun] may be called in any order, and will be called
//!   once for every @[Future] in @[futures], unless one of
//!   calls fails in which case no further calls will be
//!   performed.
Future fold(array(Future) futures,
	    mixed initial,
	    function(mixed, mixed, mixed ... : mixed) fun,
	    mixed ... extra)
{
  Promise p = Promise();
  p->depend(futures);
  p->fold(initial, fun, extra);
  return p->future();
}
