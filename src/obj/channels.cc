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

#include <sys/uio.h>

#include <cassert>

#include "channels.hh"
#include "quotas.hh"
#include "payloads.hh"
#include "scheduling.hh"

Channel::Channel(Scheduler &sched,
                 std::shared_ptr<Ingress> ingress, labelset_t labels,
                 Quota &quota,
                 const std::filesystem::path &dir)
  : ingress(ingress), labels(labels),
    queue_event(sched, std::bind(&Channel::queue_ready, this)),
    queue(100 * 1024, quota, dir, std::bind(&IdleEvent::set, queue_event)),
    current(nullptr), done(0)
{
  // TODO
}

Channel::~Channel()
{
}

bool Channel::describe(std::vector<struct iovec> &iov)
{
  if (current == nullptr) {
    current = queue.peek();
    if (!current) return false;
    done = 0;
  }
  labels_to_bytes(labels, channels, done, 0, iov);
  // TODO: Assert size within two bytes.
  length_to_bytes(current->size(), lenword, done, MAX_LABEL_BYTES, iov);
  auto m = done > MAX_LABEL_BYTES + MAX_LENGTH_BYTES
    ? MAX_LABEL_BYTES + MAX_LENGTH_BYTES + current->size() - done : current->size();
  struct iovec v = {
    .iov_base = (void *) (current->base() + (current->size() - m)),
    .iov_len = m,
  };
  iov.push_back(v);
  return true;
}

bool Channel::consumed(std::size_t done)
{
  assert(current);
  this->done += done;
  if (this->done == MAX_LABEL_BYTES + MAX_LENGTH_BYTES + current->size()) {
    queue.consume();
    current = nullptr;
    return true;
  }
  return false;
}

void Channel::failed()
{
  current = nullptr;
}

void Channel::queue_ready()
{
  ingress->ready(*this);
}

void Channel::submit(const void *base, std::size_t len)
{
  queue.push(base, len);
}
