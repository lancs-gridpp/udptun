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

#include "egress.hh"
#include "tcp_egress.hh"
#include "formatting.hh"

void Egress::activate() { }

Egress *make_egress(Scheduler &sched,
                    const std::string &egress_name,
                    const std::map<std::string, std::shared_ptr<Exit>> &refs,
                    const YAML::Node &cfg)
{
  Egress *result = nullptr;
  if (cfg["tcp"]) {
    result = new TCPEgress(egress_name, sched, cfg["tcp"]);
  }
  if (result) {
    if (cfg["channels"]) {
      auto end = cfg["channels"].end();
      for (auto iter = cfg["channels"].begin(); iter != end; iter++) {
        auto label = iter->first.as<unsigned>();
        auto cend = iter->second.end();
        for (auto citer = iter->second.begin(); citer != cend; citer++) {
          auto name = citer->as<std::string>();
          auto pos = refs.find(name);
          if (pos == refs.end()) {
            delete result;
            throw std::runtime_error(sformat("unknown exit %s for egress %s",
                                             name.c_str(), egress_name.c_str()));
          }
          result->channel(label, pos->second);
        }
      }
    }
  }
  return result;
}
