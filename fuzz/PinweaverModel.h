/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pinweaver specific model to facilitate fuzzing. */

#ifndef __FUZZ_PINWEAVERMODEL_H
#define __FUZZ_PINWEAVERMODEL_H

#include <memory>
#include <unordered_map>

extern "C" {
#include "include/pinweaver.h"
#include "include/pinweaver_types.h"
};

#include "fuzz/cr50.pb.h"

typedef std::pair <uint64_t, uint8_t> masked_label;

namespace std {
template<> struct hash<masked_label> {
  size_t operator()(const masked_label& lbl) const {
    static const auto hash_first = hash<uint64_t>();
    static const auto hash_second = hash<uint8_t>();
    return hash_first(lbl.first) * hash_second(lbl.second);
  }
};
}

class PinweaverModel {
 public:
  void SendBuffer(uint8_t* buffer_);
  void ApplyPinweaver(const fuzz::PinWeaver& pinweaver, uint8_t* buffer_);
  void Reset();
 private:
  static constexpr uint8_t EMPTY_SUBTREE = 0xff;

  struct leaf_data {
    std::vector<uint8_t> wrapped_data_;
    struct pw_request_insert_leaf_t insert_leaf_;
  };

  struct merkle_tree_t merkle_tree_;
  std::unordered_map<uint64_t, std::unique_ptr<struct leaf_data>>
      leaf_metadata_;
  /* masked_label.first is the label path, this is shifted to the right by the
   *   (bits_per_level * level)
   * masked_label.second is the level of the label (0 for leaf, height for root)
   *
   * If the level is EMPTY_SUBTREE, the label represents the level of the tree
   * (from the leaves to the root) and the value is the hash if all leaves under
   * a node at that level are empty. This allows for a sparse representation of
   * the hash cache.
   */
  std::unordered_map<masked_label, std::vector<uint8_t>>
      hash_tree_;

  void GetHmac(const std::string& fuzzer_hmac, uint64_t label,
               uint8_t hmac[PW_HASH_SIZE]);
  void GetPathHashes(const std::string& fuzzer_hashes, uint64_t label,
                     uint8_t path_hashes[][PW_HASH_SIZE]);

  void UpdatePathHashes(uint64_t label, uint8_t path_hash[PW_HASH_SIZE]);

  void HandleResetTree(const fuzz::PWResetTree& fuzzer_data,
                       struct pw_request_t* request,
                       struct pw_response_t* response);
  void HandleInsertLeaf(const fuzz::PWInsertLeaf& fuzzer_data,
                        struct pw_request_t* request,
                        struct pw_response_t* response);
  void HandleRemoveLeaf(const fuzz::PWRemoveLeaf& fuzzer_data,
                        struct pw_request_t* request,
                        struct pw_response_t* response);
};

#endif