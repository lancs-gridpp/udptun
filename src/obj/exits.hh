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

#ifndef exits_included
#define exits_included

#include <string>
#include <filesystem>

#include <yaml-cpp/yaml.h>

#include "descriptor.hh"
#include "timed.hh"
#include "payloads.hh"
#include "queues.hh"

class Quota;

struct Exit {
  virtual void activate() { }
  virtual void deliver(const void *, std::size_t) = 0;
  virtual ~Exit() = default;
};

class UDPExit : public Exit {
  int sock;
  const bool ipv4, ipv6;
  const std::string host;
  const std::string srv;

  PayloadQueue queue;

  Payload payload;
  bool accept(Payload &&);

public:
  UDPExit(Scheduler &sched,
          Quota &quota,
          const std::filesystem::path &dir,
          const YAML::Node &cfg);
  void activate();
  void deliver(const void *, std::size_t);
  ~UDPExit();
};

Exit *make_exit(Scheduler &sched, Quota &,
                const std::filesystem::path &dir, const YAML::Node &);

#endif
