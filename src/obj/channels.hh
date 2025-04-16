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

#ifndef channels_included
#define channels_included

#include <string>
#include <memory>
#include <filesystem>

#include "streamer.hh"
#include "queues.hh"
#include "idle.hh"
#include "messages.hh"
#include "logger.hh"

class Scheduler;
class Payload;
class Quota;
class Ingress;

class Channel : Streamer {
  const std::string name;
  Logger log;
  std::shared_ptr<Ingress> ingress;
  label_t label;
  IdleEvent queue_event;
  PayloadQueue queue;
  Payload *current;
  unsigned char clids[MAX_CLID_BYTES], channels[MAX_LABEL_BYTES], lenword[2];
  std::size_t done;
  void queue_ready();

  // Streamer interface
  bool describe(std::vector<struct iovec> &);
  bool consumed(std::size_t done);
  void failed();

public:
  Channel(const std::string &name,
          Scheduler &, std::shared_ptr<Ingress>, label_t,
          Quota &, const std::filesystem::path &);
  ~Channel();
  void activate();
  void submit(const void *, std::size_t);
  const std::string &identify();
};


#endif
