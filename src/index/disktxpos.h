// Copyright (c) 2019-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_INDEX_DISKTXPOS_H
#define HYPERCOIN_INDEX_DISKTXPOS_H

#include <flatfile.h>
#include <serialize.h>

struct CDiskTxPos : public FlatFilePos
{
    uint32_t nTxOffset{0}; // after header

    SERIALIZE_METHODS(CDiskTxPos, obj)
    {
        READWRITE(AsBase<FlatFilePos>(obj), VARINT(obj.nTxOffset));
    }

    CDiskTxPos(const FlatFilePos& blockIn, uint32_t nTxOffsetIn) : FlatFilePos{blockIn.nFile, blockIn.nPos}, nTxOffset{nTxOffsetIn} {
    }

    CDiskTxPos() = default;
};

#endif // HYPERCOIN_INDEX_DISKTXPOS_H
