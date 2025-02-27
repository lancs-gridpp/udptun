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
  const auto end = cfg["names"].end();
  for (auto iter = cfg["names"].begin(); iter != end; iter++) {
    auto name = iter->first.as<std::string>();
    auto dest_name = iter->second.as<std::string>();
    auto dest = dests(dest_name);
    if (!dest)
      throw std::runtime_error(sformat("unknown destination %s for exit %s",
                                       dest_name.c_str(), name.c_str()));
    out[name] = std::make_shared<Exit>(sched, quota, dir / name, emitter, dest);
  }
}

Exit::Exit(Scheduler &sched,
           Quota &quota,
           const std::filesystem::path &dir,
           std::shared_ptr<Emitter> emitter,
           std::shared_ptr<Destination> dest)
  : queue(100 * 1024, quota, dir,
          std::bind(&Exit::accept, this, std::placeholders::_1)),
    downstream_event(sched, std::bind(&Exit::downstream_ready, this)),
    okay(false), emitter(emitter), destination(dest) { }

void Exit::activate()
{
  emitter->activate();
}

bool Exit::accept(Payload &&pl)
{
  /* If we're not ready to send, ensure that we will be notified when
     ready, and indicate that we have not consumed the payload. */
  if (!okay) {
    emitter->notify(downstream_event);
    return false;
  }

  /* Try to send the payload. */
  auto rc = emitter->send(pl.base(), pl.size(), *destination.get(), 0);
  if (rc == EWOULDBLOCK || rc == EAGAIN) {
    okay = false;
    emitter->notify(downstream_event);
    return false;
  }

  /* Consume the payload. */
  pl.clear();
  return true;
}

Exit::~Exit()
{
  emitter->forget(downstream_event);
}

void Exit::deliver(const void *base, std::size_t len)
{
  queue.push(base, len);
}

void Exit::downstream_ready()
{
  okay = true;
  queue.awaken();
}
