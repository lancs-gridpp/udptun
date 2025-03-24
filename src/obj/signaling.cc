// -*- c-basic-offset: 2; indent-tabs-mode: nil -*-

/*
 * Copyright (c) 2025, Lancaster University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstring>

#include <system_error>
#include <functional>

#include "scheduling.hh"
#include "signaling.hh"
#include "formatting.hh"

void SignalEvent::reset()
{
  /* Tidy up our presence in the manager.  Don't bother clearing our
     signal number, as the caller will do it, or does not care (e.g.,
     the destructor). */
  assert(signo != 0);
  sched->signal_handlers[signo].erase(this);
  sched->update_signal(signo);
}

void SignalEvent::notify()
{
  user();
}

void SignalEvent::cancel()
{
  if (signo == 0) return;
  reset();
  signo = 0;
}

SignalEvent::SignalEvent(Scheduler &sched, user_t user)
  : Event(sched), signo(0), user(user)
{
  name("signal");
}

void SignalEvent::set(int signo)
{
  /* We will treat set(0) as a cancel(). */
  if (signo == 0) cancel();

  /* Do nothing if we're already set for this signal. */
  if (this->signo == signo) return;

  /* Clear any previous signal. */
  if (this->signo != 0) reset();

  /* Put us in the right place in the manager's table. */
  sched->signal_handlers[signo].insert(this);
  this->signo = signo;

  /* Make sure the manager is watching this signal. */
  sched->update_signal(signo);
}

SignalEvent::~SignalEvent()
{
  if (signo != 0) reset();
}
