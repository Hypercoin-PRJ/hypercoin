// Copyright (c) 2024-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_KERNEL_WARNING_H
#define HYPERCOIN_KERNEL_WARNING_H

namespace kernel {
enum class Warning {
    UNKNOWN_NEW_RULES_ACTIVATED,
    LARGE_WORK_INVALID_CHAIN,
};
} // namespace kernel
#endif // HYPERCOIN_KERNEL_WARNING_H
