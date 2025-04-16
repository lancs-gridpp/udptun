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

#include <set>

#include "destinations.hh"
#include "destbank.hh"
#include "formatting.hh"

void DestinationBank::load(const YAML::Node &singletons,
                           const YAML::Node &groups)
{
  std::map<std::string, std::shared_ptr<Destination>> singtab;
  if (singletons) {
    for (auto iter = singletons.begin(); iter != singletons.end(); iter++) {
      auto key = iter->first.as<std::string>();
      auto value = std::make_shared<Destination>(key, iter->second);
      singtab[key] = value;
    }
  }

  /* Create named sets of destinations. */
  std::map<std::string, std::set<std::shared_ptr<Destination>>> alttab;

  /* Add the singletons to the set table. */
  for (auto &ent : singtab)
    alttab[ent.first].insert(ent.second);

  /* Add the groups to the set table. */
  if (groups) {
    for (auto iter = groups.begin(); iter != groups.end(); iter++) {
      auto key = iter->first.as<std::string>();
      for (auto viter = iter->second.begin();
           viter != iter->second.end(); viter++) {
        auto name = viter->as<std::string>();
        auto pos = singtab.find(name);
        if (pos == singtab.end())
          throw std::runtime_error(sformat("unknown destination %s in group %s",
                                           name.c_str(), key.c_str()));
        alttab[key].insert(pos->second);
      }
    }
  }

  /* Convert the sets to lists. */
  for (auto &ent : alttab)
    tab.try_emplace(ent.first,
                    std::vector(ent.second.begin(), ent.second.end()));
}

std::shared_ptr<Destination> DestinationBank::seek(const std::string &name,
                                                   unsigned salt)
{
  auto pos = tab.find(name);
  if (pos == tab.end()) return nullptr;

  return pos->second[salt % pos->second.size()];
}
