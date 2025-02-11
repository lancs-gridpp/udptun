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

#include <sys/uio.h>

#include <cstring>
#include <cassert>

#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>

#include "payloads.hh"

payload_t make_payload(const unsigned char *buf, std::size_t len)
{
  auto r = new std::vector<unsigned char>(len);
  memcpy(r->data(), buf, len);
  return std::shared_ptr<std::vector<unsigned char>>(r);
}

static void save(Payload::bofstream &out,
                 const unsigned char *base, std::size_t len)
{
  unsigned char lenbytes[] = { (unsigned char) (len >> 8), (unsigned char) len };
  out.write(lenbytes, sizeof lenbytes);
  out.write(base, len);
}

void Payload::save(std::basic_ofstream<unsigned char,
                   std::char_traits<unsigned char>> &out)
{
  ::save(out, base_, len_);
}

bool Payload::load(bifstream &in)
{
  unsigned char lenbytes[2];
  in.read(lenbytes, sizeof lenbytes);
  if (in.fail()) return false;
  std::size_t len = (lenbytes[0] << 8) | lenbytes[1];
  unsigned char *base = new unsigned char[len];
  in.read(base, len);
  if (in.fail()) {
    delete[] base;
    return false;
  }
  clear();
  base_ = base;
  len_ = len;
  return true;
}

Payload::Payload(Payload &&rhs)
  : base_(rhs.base_), len_(rhs.len_)
{
  rhs.base_ = nullptr;
  rhs.len_ = 0;
}

Payload &Payload::operator =(Payload &&rhs)
{
  if (base_) delete[] base_;
  base_ = rhs.base_;
  len_ = rhs.len_;
  rhs.base_ = nullptr;
  rhs.len_ = 0;
  return *this;
}

bool Payload::get(struct iovec &into)
{
  if (!base_) return false;
  into.iov_base = base_;
  into.iov_len = len_;
  return true;
}

Payload::Payload(const unsigned char *base, std::size_t len)
  : base_(new unsigned char[len]), len_(len)
{
  memcpy(base_, base, len);
}

Payload::~Payload()
{
  if (base_)
    delete[] base_;
}

void Payload::clear()
{
  if (base_)
    delete[] base_;
  len_ = 0;
}

PayloadQueue::PayloadQueue(std::size_t max_mem,
                           const std::string &dir,
                           user_t user)
  : dir(dir), max_mem(max_mem), sz_mem(0), user(user),
    user_ready(false)
{
  /* Get the list of matching queue files. */
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    const auto &fn = entry.path();
    if (fn.extension() != ".queue") continue;
    index_t key = std::stoll(fn.stem(), nullptr, 16);
    queue_fns[key] = fn;
  }
}

PayloadQueue::index_t PayloadQueue::now_index()
{
  return std::chrono::duration_cast<std::chrono::seconds>
    (std::chrono::system_clock::now().time_since_epoch()).count();
}

std::filesystem::path PayloadQueue::make_queue_file(index_t key)
{
  std::stringstream txt;
  txt << std::hex << ".queue" << key;
  std::filesystem::path nf(dir);
  nf /= txt.str();
  return nf;
}

bool PayloadQueue::load1(Payload::bifstream &fin)
{
  Payload pl;
  if (pl.load(fin)) {
    queue.push_back(std::move(pl));
    return true;
  } else {
    return false;
  }
}

bool PayloadQueue::load_head_file()
{
  if (queue_fns.empty())
    return false;
  auto pos = queue_fns.begin();
  auto ofn = pos->second;
  Payload::bifstream fin(ofn, fin.binary);
  while (load1(fin))
    ;
  fin.close();
  queue_fns.erase(pos);
  std::filesystem::remove(ofn);
  return !queue.empty();
}


void PayloadQueue::attempt_delivery()
{
  if (!user_ready)
    return;

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

void PayloadQueue::push(const unsigned char *base, std::size_t len)
{
  if (queue_fns.empty() && sz_mem + len < max_mem) {
    /* Add the entry to memory, and account for it. */
    bool was_empty = queue.empty();
    queue.emplace_back(base, len);
    sz_mem += len;

    /* Let the user know we have a queue entry available. */
    if (was_empty)
      attempt_delivery();
    return;
  }

  if (!out.is_open() || sz_out + len + 2 > max_mem) {
    /* We need a new file.  Determine its time and name. */
    index_t key = now_index();
    auto nf = make_queue_file(key);
    queue_fns[key] = nf;

    /* Open the new file for appending, and reset the current size. */
    out.open(nf, out.out | out.app | out.binary);
    sz_out = 0;
  }

  save(out, base, len);
  sz_out += len;
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
  /* Choose a filename prior to existing ones, and save in-memory
     payloads to it. */
  index_t key = (queue_fns.empty() ? now_index() : queue_fns.begin()->first) - 1;
  auto nf = make_queue_file(key);
  out.open(nf, out.out | out.binary);
  for (auto &item : queue)
    item.save(out);
}
