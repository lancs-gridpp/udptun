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

#include <cassert>
#include <fstream>

#include "clientid.hh"

clid_t ClientTable::seek(const SocketAddress &key)
{
  auto pos = rev.find(key);
  if (pos != rev.end()) {
    auto id = pos->second;
    auto fpos = fwd.find(id);
    assert(fpos != fwd.end());
    fpos->second.update();
    return id;
  }

  for ( ; ; ) {
    auto pr = fwd.emplace(next_id, key);
    if (!pr.second) {
      next_id++;
      continue;
    }
    rev[key] = next_id;
    log.debug([next_id = this->next_id, &key](std::ostream &out) {
      out << "assigned " << next_id << " to " << key.str();
    });
    return next_id++;
  }
}

ClientTable::entry::entry(const SocketAddress &key)
  : addr(key), last_used(std::chrono::system_clock::now())
{
}

void ClientTable::entry::update()
{
  last_used = std::chrono::system_clock::now();
}

void ClientTable::on_purge()
{
  purge(last_purge);
  last_purge = std::chrono::system_clock::now();
  purge_event.set(last_purge + purge_period);
}

void ClientTable::purge(std::chrono::system_clock::time_point before)
{
  for (auto iter = fwd.begin(); iter != fwd.end(); ) {
    if (iter->second.last_used < before) {
      log.debug([iter](std::ostream &out) {
        out << "discarded " << iter->first << " to " << iter->second.addr.str();
      });
      iter = fwd.erase(iter);
      rev.erase(iter->second.addr);
    } else {
      iter++;
    }
  }
}

ClientTable::ClientTable(Scheduler &sched,
                         std::chrono::system_clock::duration purge_period,
                         const std::filesystem::path &db)
  : log("udptun.ingress.clientmap", "clientmap"),
    purge_event(sched, std::bind(&ClientTable::on_purge, this)),
    purge_period(purge_period),
    last_purge(std::chrono::system_clock::now()),
    db(db), next_id(0)
{
  purge_event.set(last_purge + purge_period);
  std::ifstream in;
  in.open(db);
  if (in.is_open()) {
    std::size_t cnt;
    in >> cnt;
    for (std::size_t i = 0; i < cnt; i++) {
      SocketAddress addr;
      clid_t clid;
      in >> clid >> addr;
      rev[addr] = clid;
      fwd.emplace(clid, addr);
    }
  }
}

ClientTable::~ClientTable()
{
  std::ofstream out(db);
  out << fwd.size() << std::endl;
  for (const auto &item : rev)
    out << item.second << " " << item.first << std::endl;
}
