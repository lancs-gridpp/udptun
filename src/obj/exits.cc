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

#include <functional>
#include <stdexcept>

#include "exits.hh"
#include "emitters.hh"
#include "formatting.hh"

void make_exits(Scheduler &sched, Quota &quota,
                const std::filesystem::path &dir, const YAML::Node &cfg,
                destination_index_t dests,
                std::map<std::string, std::shared_ptr<Exit>> &out)
{
  auto emitter = std::make_shared<Emitter>(sched, cfg["udp"]);
  const auto end = cfg["queues"].end();
  for (auto iter = cfg["queues"].begin(); iter != end; iter++) {
    auto name = iter->first.as<std::string>();
    auto dest_name = iter->second.as<std::string>();
    auto dest = dests(dest_name);
    if (!dest)
      throw std::runtime_error(sformat("unknown destination %s for exit %s",
                                       dest_name.c_str(), name.c_str()));
    out[name] =
      std::make_shared<Exit>(name, sched, quota, dir / name, emitter, dest);
  }
}

Exit::Exit(const std::string &name,
           Scheduler &sched,
           Quota &quota,
           const std::filesystem::path &dir,
           std::shared_ptr<Emitter> emitter,
           std::shared_ptr<Destination> dest)
  : name(name), log("udptun.egress.exit", std::string("egress:") + name),
    ready_event(sched, std::bind(&Exit::try_to_send, this)),
    queue(std::string("egress:") + name,
          100 * 1024, quota, dir, std::bind(&Exit::check, this)),
    emitter_user(std::bind(&Exit::check, this)),
    emitter(emitter), destination(dest)
{
  ready_event.name(sformat("exit:%s", name.c_str()));
}

void Exit::activate()
{
  emitter->activate();
  queue.poke();
}

void Exit::check()
{
  if (!queue.peek()) return;
  if (!*emitter) {
    emitter->notify(emitter_user);
    return;
  }

  /* We'll try sending on the next poll. */
  ready_event.set();
}

void Exit::try_to_send()
{
  assert(*emitter);
  Payload *payload = queue.peek();
  assert(payload);

  /* Try to send the payload. */
  auto rc = emitter->send(payload->base(),
                          payload->size(), *destination.get(), 0);
  switch (rc) {
  case 0:
    /* The payload was sent successfully.  Tell the queue not to keep
       it. */
    queue.consume();

    /* Do we have any more payloads? */
    if (queue.peek())
      /* We're ready to send another.  Tell the emitter to notify us
         when it's ready. */
      emitter->notify(emitter_user);
    return;

  case EWOULDBLOCK:
#if EAGAIN != EWOULDBLOCK
  case EAGAIN:
#endif
    emitter->notify(emitter_user);
    return;

  case EBADF:
    /* We should never be using the emitter if it's FD is not set
       up. */
    throw std::runtime_error(sformat("unreachable %s:%d", __FILE__, __LINE__));

  default:
    throw std::system_error(rc, std::system_category(), "emitter::send()");
  }
}

Exit::~Exit()
{
  emitter->forget(emitter_user);
}

void Exit::deliver(const void *base, std::size_t len)
{
  queue.push(base, len);
}
