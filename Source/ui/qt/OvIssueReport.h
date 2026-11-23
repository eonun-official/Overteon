#pragma once
#include <QtCore/QString>

// ===========================================================================
//  Log upload: create a GitHub issue from the current runtime log
//
//  Compiled into the plug-in only when the log is enabled
//  (-DOVERTEON_FILE_LOG=ON); an OFF build has neither this file nor any of its
//  call sites.  Two paths:
//
//    * With a token (OVERTEON_GITHUB_TOKEN, or one compiled in with
//      -DOVERTEON_REPORT_TOKEN=...) the log tail is uploaded straight to
//      https://api.github.com/repos/<repo>/issues and the issue URL is written
//      to the log.
//
//    * Without a token the full log is copied to the clipboard and the
//      new-issue page is opened prefilled, so the user only has to paste and
//      submit.  No credential is needed for that, and nothing is sent anywhere
//      until the user presses Submit in their browser.
//
//  A token compiled into a distributed binary can be extracted from it, so the
//  environment variable is the supported way; the build-time token exists for
//  private diagnostic builds.  Both paths redact the home directory and the
//  token itself before anything is uploaded.
// ===========================================================================
namespace ovreport
{
    /** True when a report can be built: the log build is active and the file
        exists.  QML uses this to hide the DIAGNOSTICS row in OFF builds. */
    bool available();

    /** Manual action (the Settings panel's REPORT ISSUE chip): uploads, or
        opens the prefilled issue page when no token is configured.
        Runs at most once per process. */
    void request (const QString& reason);

    /** The automatic path, used when the editor could not start.  Uploads only
        when a token is configured, or when OVERTEON_REPORT_AUTO=1 is set: an
        automatic browser tab on every user's machine would be worse than the
        bug being reported. */
    void requestAutomatic (const QString& reason);
}
