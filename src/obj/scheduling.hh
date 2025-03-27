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

#ifndef scheduling_included
#define scheduling_included

#include <signal.h>

#include <map>
#include <set>
#include <vector>
#include <chrono>

#include "priority.hh"
#include "logger.hh"

class Event;
class IdleEvent;
class TimedEvent;
class DescriptorEvent;
class SignalEvent;

class Scheduler {
  Logger log;
  int epfd, sigfd;
  std::map<std::chrono::system_clock::time_point, std::set<TimedEvent *>> table;
  std::set<IdleEvent *> idleness;
  std::map<int, std::set<SignalEvent *>> signal_handlers;

  /* 'watching' is passed to signalfd, and is updated to track the
     keys of signal_handlers.  'sigmsk' is user-supplied, and is the
     mask to use while polling. */
  sigset_t sigmsk, watching;

  std::map<prio_t, std::set<Event *>> queues;

  friend class Event;

  void enqueue(Event *);
  void dequeue(Event *);

  friend class IdleEvent;
  void set(IdleEvent *);
  void cancel(IdleEvent *);
  bool test(IdleEvent *);

  friend class TimedEvent;
  void set(TimedEvent *);
  void cancel(TimedEvent *);

  friend class DescriptorEvent;
  void add(DescriptorEvent *, bool mod);
  void remove(DescriptorEvent *);

  friend class SignalEvent;
  void update_signal(int);

  int timeout();

public:
  Scheduler();
  void signal_mask(const sigset_t &);
  void poll();
  ~Scheduler();
};

#endif
