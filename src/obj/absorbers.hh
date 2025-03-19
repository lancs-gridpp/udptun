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

#ifndef absorbers_included
#define absorbers_included

#include <string>
#include <functional>
#include <memory>
#include <set>

#include <yaml-cpp/yaml.h>

#include "descriptor.hh"

struct Absorber {
  virtual void activate() = 0;
  virtual ~Absorber();
};

class Channel;

class UDPAbsorber : public Absorber {
  const bool ipv4, ipv6;
  const std::string host, srv;
  const std::set<std::shared_ptr<Channel>> channels;

  int sock;
  DescriptorEvent sockev;
  void sock_ready(uint32_t events);
  unsigned char buf[65536];

public:
  UDPAbsorber(Scheduler &, const YAML::Node &,
              const std::set<std::shared_ptr<Channel>> &);
  ~UDPAbsorber();

  void activate();
};

typedef
std::function<std::shared_ptr<Channel>(const std::string &)> channel_index_t;

Absorber *make_absorber(Scheduler &, const std::string &, const YAML::Node &,
                        channel_index_t);

#endif
