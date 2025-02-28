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

#include <fstream>

#include <yaml-cpp/yaml.h>

#include "config.hh"

static void merge(YAML::Node &dst, const YAML::Node &src)
{
  switch (src.Type()) {
  case YAML::NodeType::Null:
  case YAML::NodeType::Undefined:
    break;

  case YAML::NodeType::Scalar:
    dst = YAML::Node(src);
    break;

  case YAML::NodeType::Sequence:
    for (auto iter = src.begin(); iter != src.end(); iter++)
      dst.push_back(YAML::Node(*iter));
    break;

  case YAML::NodeType::Map:
    for (auto iter = src.begin(); iter != src.end(); iter++) {
      auto k = iter->first.as<std::string>();
      YAML::Node ref = dst[k];
      merge(ref, iter->second);
      dst[k] = ref;
    }
    break;
  }
}

Config::Config(const std::vector<std::string> &source_files)
  : source_files(source_files) { }

YAML::Node Config::get()
{
  YAML::Node result;
  result["ingress"].push_back("tunnels");
  result["ingress"].push_back("channels");
  result["ingress"].push_back("sockets");
  result["egress"].push_back("tunnels");
  result["egress"].push_back("destinations");
  result["egress"].push_back("sockets");
  for (auto &fn : source_files) {
    YAML::Node elem = YAML::LoadFile(fn);
    merge(result, elem);
  }
  return result;
}

