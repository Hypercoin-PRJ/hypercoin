// Copyright (c) The Hypercoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <hypercoin-build-config.h> // IWYU pragma: keep

#include <common/license_info.h>

#include <tinyformat.h>
#include <util/translation.h>

#include <string>

std::string CopyrightHolders(const std::string& strPrefix)
{
    const auto copyright_devs = strprintf(_(COPYRIGHT_HOLDERS), COPYRIGHT_HOLDERS_SUBSTITUTION).translated;
    std::string strCopyrightHolders = strPrefix + copyright_devs;

    // Make sure Bitcoin Core copyright is not removed by accident
    if (copyright_devs.find("Hypercoin Core") == std::string::npos) {
        strCopyrightHolders += "\n" + strPrefix + "The Hypercoin Core developers";
    }
    std::string strYear = strPrefix;
    strYear.replace(strYear.find("2025"), sizeof("2025")-1, "2009");

    strCopyrightHolders += "\n" + strYear + "The Bitcoin Core developers";
    return strCopyrightHolders;
}

std::string LicenseInfo()
{
    const std::string URL_SOURCE_CODE = "<https://github.com/hypercoin-prj/hypercoin>";
    const std::string URL_WEBSITE = "<https://hypercoin.network>";
    return CopyrightHolders(strprintf(_("Copyright (C) %i-%i"), 2025, COPYRIGHT_YEAR).translated + " ") + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                        "Visit %s for further information about the software."),
                 CLIENT_NAME,URL_WEBSITE)
            .translated +
           "\n" +
           strprintf(_("The source code is available from %s."), URL_SOURCE_CODE).translated +
           "\n" +
           "\n" +
           _("This is experimental software.") + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s"), "COPYING", "<https://opensource.org/licenses/MIT>").translated +
           "\n";
}