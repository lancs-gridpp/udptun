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

#ifndef clientid_included
#define clientid_included

#include <map>
#include <filesystem>
#include <chrono>

#include "sockaddrs.hh"
#include "messages.hh"
#include "timed.hh"

class Scheduler;

class ClientTable {
  TimedEvent purge_event;
  const std::chrono::system_clock::duration purge_period;
  std::chrono::system_clock::time_point last_purge;
  const std::filesystem::path db;
  clientid_t next_id;

  struct entry {
    SocketAddress addr;
    std::chrono::system_clock::time_point last_used;
    entry(const SocketAddress &);
    void update();
  };
  std::map<clientid_t, entry> fwd;
  std::map<SocketAddress, clientid_t> rev;

  void on_purge();
  void purge(std::chrono::system_clock::time_point);

public:
  ClientTable(Scheduler &,
              std::chrono::system_clock::duration purge_period,
              const std::filesystem::path &);
  ~ClientTable();
  clientid_t seek(const SocketAddress &);
};

#endif
