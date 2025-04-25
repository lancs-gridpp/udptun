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

#include <sys/epoll.h>

#include <cassert>

#include <system_error>
#include <sstream>

#include "formatting.hh"
#include "descriptor.hh"
#include "scheduling.hh"

void epoll_event_out(std::ostream &out, uint32_t events)
{
  if (events & EPOLLIN) out << " IN";
  if (events & EPOLLOUT) out << " OUT";
  if (events & EPOLLRDHUP) out << " RDHUP";
  if (events & EPOLLPRI) out << " PRI";
  if (events & EPOLLERR) out << " ERR";
  if (events & EPOLLHUP) out << " HUP";
  if (events & EPOLLET) out << " ET";
  if (events & EPOLLONESHOT) out << " ONESHOT";
  if (events & EPOLLWAKEUP) out << " WAKEUP";
  if (events & EPOLLEXCLUSIVE) out << " EXCLUSIVE";
}

std::string epoll_event_str(uint32_t events)
{
  std::stringstream out;
  epoll_event_out(out, events);
  return out.str();
}

DescriptorEvent::DescriptorEvent(Scheduler &sched, descriptor_handler_t action)
  : Event(sched), action(action), fd(-1)
{
  name("descriptor");
}

void DescriptorEvent::set(int fd, uint32_t events)
{
  if (fd < 0)
    throw std::invalid_argument(sformat("bad fd %d", fd));
  expected = events;
  this->got = 0;
  if (fd != this->fd) {
    sched->remove(this);
    this->fd = fd;
    sched->add(this, false);
  } else {
    sched->add(this, true);
  }
  sched->dequeue(this);
}

void DescriptorEvent::cancel()
{
  sched->remove(this);
  fd = -1;
  sched->dequeue(this);
}

void DescriptorEvent::notify()
{
  action(got);
}

void DescriptorEvent::update(uint32_t events)
{
  got |= events;
  expected &= ~events;
  sched->add(this, true);
}

void Scheduler::add(DescriptorEvent *mom, bool mod)
{
  assert(mom->fd >= 0);
  struct epoll_event epe;
  epe.events = mom->expected;
  epe.data.ptr = mom;
  if (epoll_ctl(epfd, mod ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, mom->fd, &epe) < 0)
    throw std::system_error(errno, std::system_category(),
                            mod ? "epoll_ctl(ADD)" : "epoll_ctl(ADD)");
}

void Scheduler::remove(DescriptorEvent *mom)
{
  if (mom->fd < 0) return;
  struct epoll_event epe; // unused
  if (epoll_ctl(epfd, EPOLL_CTL_DEL, mom->fd, &epe) < 0)
    throw std::system_error(errno, std::system_category(), "epoll_ctl(DEL)");
}
