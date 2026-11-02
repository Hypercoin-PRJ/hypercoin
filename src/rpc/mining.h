// Copyright (c) 2020-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_RPC_MINING_H
#define HYPERCOIN_RPC_MINING_H

#include <cstdint>

/** Default max iterations to try in RPC generatetodescriptor, generatetoaddress, and generateblock. */
inline constexpr uint64_t DEFAULT_MAX_TRIES{1'000'000};

#endif // HYPERCOIN_RPC_MINING_H
