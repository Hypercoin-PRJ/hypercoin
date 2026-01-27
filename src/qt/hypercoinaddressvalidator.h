// Copyright (c) 2011-2020 The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef HYPERCOIN_QT_HYPERCOINADDRESSVALIDATOR_H
#define HYPERCOIN_QT_HYPERCOINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class HypercoinAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit HypercoinAddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** Hypercoin address widget validator, checks for a valid hypercoin address.
 */
class HypercoinAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit HypercoinAddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // HYPERCOIN_QT_HYPERCOINADDRESSVALIDATOR_H
