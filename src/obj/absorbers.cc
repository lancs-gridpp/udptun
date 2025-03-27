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

#include "absorbers.hh"
#include "udp_absorbers.hh"
#include "formatting.hh"

Absorber::~Absorber() { }

Absorber *make_absorber(Scheduler &sched,
                        const std::string &inst,
                        const YAML::Node &cfg,
                        channel_index_t channels)
{
  const auto &channels_root = cfg["channels"];
  if (!channels_root) return nullptr;

  std::set<std::shared_ptr<Channel>> channel_set;
  for (auto iter = channels_root.begin(); iter != channels_root.end(); iter++) {
    const auto &cname = iter->as<std::string>();
    auto pos = channels(cname);
    if (!pos)
      throw std::runtime_error(sformat("unknown channel %s for socket %s",
                                       cname.c_str(), inst.c_str()));
    channel_set.insert(pos);
  }
  if (channel_set.empty())
    return nullptr;

  if (cfg["udp"])
    return new UDPAbsorber(inst, sched, cfg["udp"], channel_set);

  return nullptr;
}
