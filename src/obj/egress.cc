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
#include "messages.hh"
#include "tcp_egress.hh"
#include "formatting.hh"

void Egress::activate() { }

Egress *make_egress(Scheduler &sched,
                    Quota &quota,
                    const std::filesystem::path &qdir,
                    const std::string &egress_name,
                    PeerTable *peers_backup,
                    DestinationBank &dests,
                    const YAML::Node &cfg)
{
  Egress *result = nullptr;
  channelmap_t channels;

  /* Extract names for the labels. */
  const auto &label_root = cfg["labels"];
  if (label_root) {
    for (auto iter = label_root.begin(); iter != label_root.end(); iter++) {
      auto label = iter->second.as<label_t>();
      channels[label].name = iter->first.as<std::string>();
    }
  }

  /* List the channels that each label should go to. */
  const auto &chroot = cfg["channels"];
  if (chroot) {
    for (auto iter = label_root.begin(); iter != label_root.end(); iter++) {
      auto qname = iter->first.as<std::string>();
      auto label = iter->second.as<label_t>();
      channels[label].dests.insert(qname);
    }
  }

  if (cfg["tcp"]) {
    result = new TCPEgress(egress_name, sched, quota,
                           qdir / egress_name, peers_backup,
                           dests, channels, cfg["tcp"]);
  }
  return result;
}
