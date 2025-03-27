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

#include <cassert>
#include <stdexcept>

#include "timed.hh"
#include "scheduling.hh"
#include "formatting.hh"

#include <iostream>

TimedEvent::TimedEvent(Scheduler &sched, timed_handler_t action)
  : Event(sched), action(action), set_(false)
{
  name("timed");
}

void TimedEvent::notify()
{
  action();
}

void TimedEvent::set(const std::chrono::system_clock::time_point &when)
{
  if (set_) {
    if (this->when == when) return;
    sched->cancel(this);
  }
  this->when = when;
  set_ = true;
  sched->set(this);
}

void TimedEvent::cancel()
{
  sched->dequeue(this);
  if (!set_) return;
  sched->cancel(this);
  set_ = false;
}

void Scheduler::set(TimedEvent *mom)
{
  assert(mom->set_);
  table[mom->when].insert(mom);
}

void Scheduler::cancel(TimedEvent *mom)
{
  assert(mom->set_);
  table[mom->when].erase(mom);
}
