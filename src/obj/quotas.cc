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

#include <algorithm>

#include "quotas.hh"

Quota::Quota() : total(0), max(0) { }

void Quota::set(size_t sz)
{
  max = sz;
  check();
}

void Quota::check()
{
  for (auto iter = ages.begin(); total > max && iter != ages.end(); iter++) {
    for (auto up : iter->second) {
      up->discard();
      break;
    }
  }
}

void Quota::increase(User &user, size_t sz)
{
  auto pos = users.try_emplace(&user);
  auto &counter = pos.first->second.amount;
  counter += sz;
  total += sz;
  check();
}

void Quota::decrease(User &user, size_t sz)
{
  auto pos = users.try_emplace(&user);
  auto &counter = pos.first->second.amount;
  size_t am;
  if (counter < sz) {
    am = counter;
    counter = 0;
  } else {
    am = sz;
    counter -= sz;
  }
  if (total < am)
    total = 0;
  else
    total -= am;
}

void Quota::forget(User &user)
{
  /* Find the user entry.  Do nothing if not there. */
  auto pos = users.find(&user);
  if (pos == users.end()) return;

  /* Now we know what the user's age is, see if we have an entry for
     it in the age sequence. */
  erase_from_sequence(user, pos->second.age);

  /* Account for the loss of contribution against the quota, and
     remove the user's entry. */
  if (total < pos->second.amount)
    total = 0;
  else
    total -= pos->second.amount;
  users.erase(pos);
}

void Quota::erase_from_sequence(User &user, epochtime_t when)
{
  /* Zero is used to indicate a lack of time. */
  if (when == 0) return;

  auto tpos = ages.find(when);
  if (tpos != ages.end()) {
    /* Find the age-sequence entry, and delete it. */
    if (tpos->second.erase(&user) > 0) {
      /* Delete the age-sequence entry if no longer required. */
      if (tpos->second.empty())
        ages.erase(tpos);
    }
  }
}

void Quota::insert_into_sequence(User &user, epochtime_t when)
{
  assert(when != 0);
  ages[when].insert(&user);
}

void Quota::oldest(User &user, epochtime_t when)
{
  auto pos = users.try_emplace(&user);
  auto &data = pos.first->second;
  if (data.age != when) {
    /* Take the user out of its current position. */
    erase_from_sequence(user, data.age);

    /* Record its new position. */
    data.age = when;
    if (when != 0)
      insert_into_sequence(user, when);
  }
}
