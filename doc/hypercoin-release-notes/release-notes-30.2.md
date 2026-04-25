30.2.0 Release Notes
====================

Hypercoin Core version 30.2.0 is now available from:

  <https://hypercoin.network>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.


Downgrade warning
------------------

Because release 30.0+ and later makes use of headers-first synchronization and
parallel block download (see further), the block files and databases are not
backwards-compatible with pre-30.0 versions of Hypercoin Core or other software:

* Blocks will be stored on disk out of order (in the order they are
received, really), which makes it incompatible with some tools or
other programs. Reindexing using earlier versions will also not work
anymore as a result of this.

* The block index database will now hold headers for which no block is
stored on disk, which earlier versions won't support.

If you want to be able to downgrade smoothly, make a backup of your entire data
directory. Without this your node will need start syncing (or importing from
bootstrap.dat) anew afterwards. 

This does not affect wallet forward or backward compatibility.

Hypercoin 30.2.0 Change log
=========================
This release is based upon Bitcoin Core v30.2  Their upstream changelog applies to us and
is included in as separate release notes. This section describes the hypercoin-specific differences.

Protocol:

- Keep Sha256d Proof-of-Work.
- Hypercoin TCP port 8993 (instead of 8333)
- RPC TCP port 8992 (instead of 8332)
- Testnet TCP port 18993 (instead of 18333)
- Testnet RPC TCP port 18992 (instead of 18332)
- Signet TCP port 38993 (instead of 38333)
- Signet RPC TCP port 38992 (instead of 38332)
- Magic 0xf7f7f7f7       (instead of 0xf9beb4d9)
- Target Block Time 1 minute (instead of 10 minutes)
- Target Timespan 1 hour      (instead of two weeks)
- Max Block Weight 16000000 (instead of 4000000)
- Max Block Serialized Size 8000000 (instead of 4000000)
- Coinbase Maturity 30 (instead of 100)
- nbits 0x1e0ffff0 (instead of 0x1d00ffff)
- 21 Million Coin Limit 
- SubsidyHalvingInterval 4years 

Credits
=======

Thanks to everyone who directly contributed to this release:

* HRC developers
* BTC developers


