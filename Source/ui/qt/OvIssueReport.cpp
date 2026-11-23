#include "OvIssueReport.h"
#include "OvRuntimeLog.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <atomic>
#include <utility>

namespace
{
    std::atomic<bool> started { false };

    QString toQt (const juce::String& text)
    {
        return QString::fromUtf8 (text.toRawUTF8());
    }

    /** OVERTEON_GITHUB_TOKEN wins so a machine can override a private build. */
    QString token()
    {
        const auto fromEnvironment = qEnvironmentVariable ("OVERTEON_GITHUB_TOKEN");
        if (! fromEnvironment.isEmpty())
            return fromEnvironment;
       #ifdef OVERTEON_REPORT_TOKEN
        return QStringLiteral (OVERTEON_REPORT_TOKEN);
       #else
        return {};
       #endif
    }

    QString apiBase()
    {
        auto base = qEnvironmentVariable ("OVERTEON_REPORT_API_BASE");
        if (base.isEmpty())
            base = QStringLiteral ("https://api.github.com");
        while (base.endsWith (QLatin1Char ('/')))
            base.chop (1);
        return base;
    }

    QString repository()
    {
        auto repo = qEnvironmentVariable ("OVERTEON_REPORT_REPO");
        if (repo.isEmpty())
            repo = QStringLiteral ("eonun-official/Overteon");
        return repo;
    }

    /** What must never leave the machine in clear text. */
    QString redact (QString text, const QString& secret)
    {
        // The running user's home plus any other user's home that appears in a
        // log line (build paths, library paths): both become "~" or "~\\".
        text.replace (QDir::homePath(), QStringLiteral ("~"));
        text.replace (QRegularExpression (QStringLiteral ("/(?:home|Users)/[^/\\s]+/")),
                      QStringLiteral ("~/"));
        text.replace (QRegularExpression (QStringLiteral ("[A-Za-z]:\\\\Users\\\\[^\\\\\\s]+\\\\")),
                      QStringLiteral ("~\\\\"));
        if (! secret.isEmpty())
            text.replace (secret, QStringLiteral ("[token]"));
        return text;
    }

    QStringList readLogLines()
    {
        QFile input (toQt (ovlog::file().getFullPathName()));
        if (! input.open (QIODevice::ReadOnly | QIODevice::Text))
            return {};
        return QString::fromUtf8 (input.readAll()).split (QLatin1Char ('\n'), Qt::KeepEmptyParts);
    }

    QString joined (const QStringList& lines, int from, int count)
    {
        const int end = qMin (lines.size(), from + count);
        return end <= from ? QString() : lines.mid (from, end - from).join (QLatin1Char ('\n'));
    }

    QString shortened (QString text, int maximum)
    {
        if (text.size() <= maximum)
            return text;
        return text.left (maximum - 40) + QStringLiteral ("\n... [truncated] ...");
    }

    QString titleFor (const QString& reason)
    {
        return redact (QStringLiteral ("[Overteon log] %1 — Overteon %2, Qt %3, %4")
                           .arg (reason, QStringLiteral ("0.9.20"), QString::fromLatin1 (qVersion ()),
                                 QGuiApplication::platformName()), {});
    }

    /** Markdown body: a short header, the start-up environment, the log tail. */
    QString bodyFor (const QString& reason, const QStringList& lines, const QString& secret)
    {
        QString body;
        body += QStringLiteral ("**Reason:** %1\n").arg (reason);
        body += QStringLiteral ("**Requested:** %1\n")
                    .arg (QDateTime::currentDateTimeUtc().toString (Qt::ISODate));
        body += QStringLiteral ("**Log file:** `%1` (tail below)\n\n")
                    .arg (redact (toQt (ovlog::file().getFullPathName()), secret));
        body += QStringLiteral ("<details><summary>Environment (start of the log)</summary>\n\n")
                + QStringLiteral ("```text\n") + redact (joined (lines, 0, 90), secret)
                + QStringLiteral ("\n```\n\n</details>\n\n");
        body += QStringLiteral ("```text\n")
                + redact (joined (lines, qMax (0, lines.size() - 400), 400), secret)
                + QStringLiteral ("\n```\n");

        return shortened (body, 60000);
    }

    void uploadToApi (const QString& apiBase, const QString& repository, const QString& secret,
                      const QString& title, const QString& body)
    {
        QJsonObject payload;
        payload.insert (QStringLiteral ("title"), title);
        payload.insert (QStringLiteral ("body"), body);
        const QByteArray json = QJsonDocument (payload).toJson (QJsonDocument::Compact);

        const QUrl url (apiBase + QStringLiteral ("/repos/") + repository + QStringLiteral ("/issues"));
        QNetworkRequest request (url);
        request.setHeader (QNetworkRequest::ContentTypeHeader, QStringLiteral ("application/json"));
        request.setRawHeader ("Accept", "application/vnd.github+json");
        request.setRawHeader ("User-Agent", "Overteon-Diagnostics/1.0");
        request.setRawHeader ("X-GitHub-Api-Version", "2022-11-28");
        request.setRawHeader ("Authorization", QByteArray ("Bearer ") + secret.toUtf8());

        auto* manager = new QNetworkAccessManager();
        auto* reply = manager->post (request, json);
        ovlog::info ("report: uploading " + juce::String (json.size()) + " bytes to "
                     + juce::String (url.toString().toUtf8().constData()));

        QObject::connect (reply, &QNetworkReply::finished, reply, [reply, manager] {
            const int status = reply->attribute (QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            if (status == 201)
            {
                const auto htmlUrl = QJsonDocument::fromJson (data).object()
                                         .value (QStringLiteral ("html_url")).toString();
                ovlog::info ("report: issue created " + juce::String (htmlUrl.toUtf8().constData()));
            }
            else
            {
                ovlog::error ("report: GitHub returned HTTP " + juce::String (status) + ": "
                              + juce::String (QString::fromUtf8 (data.left (2000)).toUtf8().constData ()));
            }
            reply->deleteLater();
            manager->deleteLater();
        });
    }

    void openIssuePage (const QString& repository, const QString& secret,
                        const QString& title, const QStringList& lines)
    {
        // Nothing is sent from here: the log goes to the clipboard (so Cmd/Ctrl+V
        // in the issue body attaches it at full length) and the browser opens
        // the prefilled new-issue page.
        if (auto* clipboard = QGuiApplication::clipboard())
            clipboard->setText (redact (lines.join (QLatin1Char ('\n')), secret).left (400000));

        QUrl url (QStringLiteral ("https://github.com/") + repository + QStringLiteral ("/issues/new"));
        QUrlQuery query;
        query.addQueryItem (QStringLiteral ("title"), title);
        query.addQueryItem (QStringLiteral ("body"),
                            QStringLiteral ("**Reason:** requested from the Overteon UI\n\n"
                                            "The full runtime log is on the clipboard — paste it "
                                            "below (Ctrl/Cmd+V) and review it before submitting.\n\n"
                                            "Log file: `%1`\n")
                                .arg (redact (toQt (ovlog::file().getFullPathName()), secret)));
        url.setQuery (query);

        // Opening a browser is a user-visible side effect, so it is off in
        // headless/automated runs: OVERTEON_REPORT_NO_BROWSER (and the test
        // hook) never launch one.  The URL is logged either way, and the log
        // is already on the clipboard.
        const bool openBrowser = ! qEnvironmentVariableIsSet ("OVERTEON_REPORT_NO_BROWSER")
                              && ! qEnvironmentVariableIsSet ("OVERTEON_REPORT_TEST");
        if (openBrowser)
            QDesktopServices::openUrl (url);

        ovlog::info (openBrowser
                     ? "report: no token configured; log copied to the clipboard and the new-issue "
                       "page opened: " + juce::String (url.toString (QUrl::FullyEncoded).toUtf8().constData())
                     : "report: no token configured; browser disabled, log copied to the clipboard, "
                       "issue page: " + juce::String (url.toString (QUrl::FullyEncoded).toUtf8().constData()));
    }
}

namespace ovreport
{
bool available()
{
    if (! ovlog::enabled())
        return false;
    return ovlog::file().existsAsFile();
}

void request (const QString& reason)
{
    if (! available())
    {
        ovlog::warn ("report: skipped, no runtime log is being written");
        return;
    }

    if (qEnvironmentVariableIsSet ("OVERTEON_REPORT_DISABLE"))
    {
        ovlog::info ("report: disabled by OVERTEON_REPORT_DISABLE");
        return;
    }

    if (started.exchange (true))
    {
        ovlog::info ("report: a report was already sent (or is in flight) this session");
        return;
    }

    const auto secret = token();
    const auto lines = readLogLines();
    const auto title = titleFor (reason);
    const auto body = bodyFor (reason, lines, secret);

    ovlog::info ("report: collecting diagnostics (" + juce::String (lines.size())
                 + " log lines, token " + (secret.isEmpty() ? "absent" : "present") + ")");

    if (! secret.isEmpty())
        uploadToApi (apiBase(), repository(), secret, title, body);
    else
        openIssuePage (repository(), secret, title, lines);
}

void requestAutomatic (const QString& reason)
{
    const bool forced = qEnvironmentVariable ("OVERTEON_REPORT_AUTO") == QStringLiteral ("1");
    if (token().isEmpty() && ! forced)
    {
        ovlog::info ("report: automatic upload skipped (no token; set OVERTEON_GITHUB_TOKEN, "
                     "or OVERTEON_REPORT_AUTO=1 to open the prefilled issue page instead)");
        return;
    }
    request (reason);
}
} // namespace ovreport
