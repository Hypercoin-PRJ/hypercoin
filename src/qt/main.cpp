// Copyright (c) 2018-present The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/hypercoin.h>

#include <compat/compat.h>
#include <util/translation.h>

#include <QCoreApplication>

#include <functional>
#include <string>

/** Translate string to current locale using Qt. */
extern const TranslateFn G_TRANSLATION_FUN = [](const char* psz) {
    return QCoreApplication::translate("hypercoin-core", psz).toStdString();
};

const std::function<std::string()> G_TEST_GET_FULL_NAME{};

MAIN_FUNCTION
{
    return GuiMain(argc, argv);
}
