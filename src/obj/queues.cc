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

#include <filesystem>
#include <fstream>
#include <functional>

#include "payloads.hh"
#include "queues.hh"

PayloadQueue::PayloadQueue(const std::string &name,
                           std::size_t max_mem, Quota &quota,
                           const std::filesystem::path &dir,
                           user_t user)
  : name(name), log("udptun.queue", std::string("queue:") + name),
    dir(dir), max_mem(max_mem), quota(quota),
    quota_user(std::bind(&PayloadQueue::discard_file, this)),
    sz_mem(0), user(user), disappointed(true)
{
  /* Get the list of matching queue files, and sum up their sizes. */
  std::filesystem::create_directory(dir);
  Quota::size_t tsz = 0;
  index_t oldest_key = 0;
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    /* Match only files with a certain suffix. */
    const auto &fn = entry.path();
    if (fn.extension() != ".queue") continue;

    /* Use the filename stem to determine the sequence number by which
       the filename is indexed. */
    index_t key = std::stoll(fn.stem(), nullptr, 16);
    queue_fns[key] = fn;
    if (oldest_key != 0 || key < oldest_key) oldest_key = key;

    /* Accumulate the file size. */
    auto fsz = std::filesystem::file_size(fn);
    tsz += fsz;
  }

  /* Report the total to the quota manager. */
  quota.increase(quota_user, tsz);
  quota.oldest(quota_user, oldest_key);
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
  /* Load at least one non-empty queue file, if available. */
  do {
    /* Fail if there's no more files to load from. */
    if (queue_fns.empty())
      return false;

    /* Get the earliest filename. */
    auto pos = queue_fns.begin();
    auto &ofn = pos->second;

    /* Read in the contents into the in-memory queue. */
    std::ifstream fin(ofn, std::ios::binary);
    Quota::size_t sum = 0;
    while (load1(fin, sum))
      ;
    fin.close();

    /* Delete the file and its entry, and notify thw quota manager of
       the reduction in disc usage. */
    std::filesystem::remove(ofn);
    queue_fns.erase(pos);
    quota.decrease(quota_user, sum);

    /* Keep trying if we still don't have any payloads. */
  } while (queue.empty());

  /* We succeeded in loading at least one payload. */
  return true;
}

void PayloadQueue::discard_file()
{
  /* Delete the oldest file. */
  auto pos = queue_fns.begin();
  if (pos == queue_fns.end()) return;
  auto ofn = pos->second;
  auto sz = std::filesystem::file_size(ofn);
  std::filesystem::remove(ofn);
  queue_fns.erase(pos);
  quota.decrease(quota_user, sz);
}

void PayloadQueue::poke()
{
  if (peek()) user();
}

Payload *PayloadQueue::peek()
{
  /* Provide a pointer to the head of the queue if present.  If not,
     record that the user would like to be notified when data's
     ready, and then return null. */
  if (queue.empty() && !load_head_file()) {
    /* The in-memory queue is empty, and we failed to populate it from
       disc. */
    disappointed = true;
    return nullptr;
  }

  auto hd = queue.begin();
  assert(hd != queue.end());
  return &*hd;
}

void PayloadQueue::consume()
{
  /* Remove the head element if present. */
  auto pos = queue.begin();
  if (pos != queue.end())
    queue.erase(pos);
}

void PayloadQueue::push(const void *base, std::size_t len)
{
  if (queue_fns.empty() && sz_mem + len < max_mem) {
    /* Add the entry to memory, and account for it. */
    bool was_empty = queue.empty();
    queue.emplace_back(base, len);
    sz_mem += len;

    /* Let the user know we have a queue entry available. */
    if (was_empty && disappointed) {
      disappointed = false;
      user();
    }
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

  /* Append the payload to the latest file.  Keep track of the file
     size, so we'll know when to start on a new file.  Update the
     quota manager about the increase in disc usage. */
  Payload::save(out, base, len);
  sz_out += len + 2;
  if (sz_out >= max_mem)
    out.close();
  quota.increase(quota_user, len + 2);
}

PayloadQueue::~PayloadQueue()
{
  if (!queue.empty()) {
    /* Choose a filename prior to existing ones, and save in-memory
       payloads to it.  DON'T update the quota manager, as we're about
       to be destroyed, and we can't do anything about it now
       anyway. */
    index_t key =
      (queue_fns.empty() ? now_index() : queue_fns.begin()->first) - 1;
    auto nf = make_queue_file(key);
    if (out.is_open()) out.close();
    out.open(nf, std::ios::binary);
    for (auto &item : queue)
      item.save(out);
    out.close();
    queue_fns[key] = nf;
  }

  /* Update the quota manager that our remaining files don't count. */
  Quota::size_t sum = 0;
  for (auto &kp : queue_fns)
    sum += std::filesystem::file_size(kp.second);
  quota.decrease(quota_user, sum);

  /* Our quota entry should be discarded. */
  quota.forget(quota_user);
}
