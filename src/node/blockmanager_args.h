
// Copyright (c) 2023 The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_NODE_BLOCKMANAGER_ARGS_H
#define HYPERCOIN_NODE_BLOCKMANAGER_ARGS_H

#include <node/blockstorage.h>
#include <util/result.h>

class ArgsManager;

namespace node {
[[nodiscard]] util::Result<void> ApplyArgsManOptions(const ArgsManager& args, BlockManager::Options& opts);
} // namespace node

#endif // HYPERCOIN_NODE_BLOCKMANAGER_ARGS_H
