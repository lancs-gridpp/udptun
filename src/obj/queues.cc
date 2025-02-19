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

#include <filesystem>
#include <fstream>
#include <cassert>

#include "payloads.hh"
#include "queues.hh"

PayloadQueue::PayloadQueue(std::size_t max_mem, Quota &quota,
                           const std::filesystem::path &dir,
                           user_t user)
  : dir(dir), max_mem(max_mem), quota(quota), sz_mem(0), user(user),
    user_ready(false)
{
  /* Get the list of matching queue files. */
  std::filesystem::create_directory(dir);
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto &fn = entry.path();
    if (fn.extension() != ".queue") continue;
    index_t key = std::stoll(fn.stem(), nullptr, 16);
    queue_fns[key] = fn;
  }
}

PayloadQueue::index_t PayloadQueue::now_index()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count();
}

std::filesystem::path PayloadQueue::make_queue_file(index_t key)
{
  std::stringstream txt;
  txt << std::hex << key << ".queue";
  std::filesystem::path nf(dir);
  nf /= txt.str();
  return nf;
}

bool PayloadQueue::load1(std::ifstream &fin, std::size_t &sum)
{
  Payload pl;
  if (pl.load(fin, sum)) {
    queue.push_back(std::move(pl));
    return true;
  } else {
    return false;
  }
}

bool PayloadQueue::load_head_file()
{
  do {
    if (queue_fns.empty())
      return false;
    auto pos = queue_fns.begin();
    auto &ofn = pos->second;
    std::ifstream fin(ofn, std::ios::binary);
    std::size_t sum = 0;
    while (load1(fin, sum))
      ;
    fin.close();
    std::filesystem::remove(ofn);
    queue_fns.erase(pos);
  } while (queue.empty());
  return true;
}


void PayloadQueue::attempt_delivery()
{
  assert(user_ready);

  /* Offer items from the in-memory queue until refused. */
  for ( ; ; ) {
    /* If the in-memory queue is empty, load and discard one of the
       files. */
    if (queue.empty() && !load_head_file())
      return;
    assert(!queue.empty());

    auto pos = queue.begin();
    if (user(std::move(*pos))) {
      queue.erase(pos);
      continue;
    }

    /* The user is not accepting any more payloads for now. */
    user_ready = false;
    break;
  }
}

void PayloadQueue::push(const void *base, std::size_t len)
{
  if (queue_fns.empty() && sz_mem + len < max_mem) {
    /* Add the entry to memory, and account for it. */
    bool was_empty = queue.empty();
    queue.emplace_back(base, len);
    sz_mem += len;

    /* Let the user know we have a queue entry available. */
    if (was_empty && user_ready)
      attempt_delivery();
    return;
  }

  if (!out.is_open() || sz_out + (2 + len) >= max_mem) {
    /* We need a new file.  Determine its time and name. */
    index_t key = now_index();
    auto nf = make_queue_file(key);
    queue_fns[key] = nf;

    /* Open the new file for appending, and reset the current size. */
    if (out.is_open()) out.close();
    out.open(nf, std::ios::binary);
    sz_out = 0;
  }

  Payload::save(out, base, len);
  sz_out += len + 2;
  if (sz_out >= max_mem)
    out.close();
}

void PayloadQueue::awaken()
{
  user_ready = true;
  attempt_delivery();
}

PayloadQueue::~PayloadQueue()
{
  if (!queue.empty()) {
    /* Choose a filename prior to existing ones, and save in-memory
       payloads to it. */
    index_t key =
      (queue_fns.empty() ? now_index() : queue_fns.begin()->first) - 1;
    auto nf = make_queue_file(key);
    if (out.is_open()) out.close();
    out.open(nf, std::ios::binary);
    for (auto &item : queue)
      item.save(out);
  }
}
