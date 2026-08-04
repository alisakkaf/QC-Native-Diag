#ifndef VERSION_H
#define VERSION_H

#include <QString>
#include <QDate>

/**
 * @file Version.h
 * @brief Centralized Application Versioning & Developer Metadata for Mini Diag Tool.
 * Modifying values here updates all UI titles, About dialog, build dates, and URLs project-wide.
 */

namespace VersionInfo {
    // --- Application Version & Branding ---
    constexpr const char* APP_NAME           = "Mini Diag";
    constexpr const char* APP_DEVELOPER      = "Ali_Sakkaf";
    constexpr const char* APP_DEVELOPER_FULL = "Ali Sakkaf";
    constexpr const char* APP_VERSION        = "1.1.0.0";
    constexpr const char* APP_STATUS         = "BETA";

    // --- Official Websites & Social Links ---
    constexpr const char* URL_WEBSITE        = "https://alisakkaf.com/";
    constexpr const char* URL_GITHUB_PROFILE = "https://github.com/alisakkaf/";
    constexpr const char* URL_GITHUB_REPO    = "https://github.com/alisakkaf/QC-Native-Diag";
    constexpr const char* URL_FACEBOOK       = "https://www.facebook.com/AliSakkaf.Dev/";

    // --- Dynamic Helper Functions ---

    /**
     * @brief Generates current build date string formatted as yyyy-MM-dd
     */
    inline QString getBuildDate() {
        return QDate::currentDate().toString("yyyy-MM-dd");
    }

    /**
     * @brief Generates complete standardized main window title
     * Format: Mini Diag - By Ali_Sakkaf | V1.1.0.0 (BETA) | yyyy-MM-dd | Active: SIM 1
     */
    inline QString getWindowTitle(const QString &activeSim = QString()) {
        QString title = QString("%1 - By %2 | V%3 (%4) | %5")
                            .arg(APP_NAME)
                            .arg(APP_DEVELOPER)
                            .arg(APP_VERSION)
                            .arg(APP_STATUS)
                            .arg(getBuildDate());

        if (!activeSim.isEmpty()) {
            title += QString(" | Active: %1").arg(activeSim);
        }
        return title;
    }
}

#endif // VERSION_H
