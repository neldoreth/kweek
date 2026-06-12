#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Minimal generic CalDAV client built on QNetworkAccessManager.
 *
 * Handles discovery of calendar collections (current-user-principal ->
 * calendar-home-set -> collection listing) and basic fetch/put/delete of
 * VEVENT resources via REPORT/PUT/DELETE. Works against any RFC 4791
 * compliant server (Apple iCloud, Nextcloud, Fastmail, ...).
 *
 * All requests use HTTP Basic auth, which is what iCloud app-specific
 * passwords, Nextcloud app passwords and Fastmail app passwords expect.
 */
class CalDavClient : public QObject
{
    Q_OBJECT

public:
    struct CalendarInfo {
        QString url;
        QString displayName;
        QString color;
    };

    struct RemoteEvent {
        QString href;
        QString etag;
        QString data;
    };

    explicit CalDavClient(QObject *parent = nullptr);

    /// Discovers the calendar collections available to this account.
    void discoverCalendars(const QUrl &serverUrl, const QString &username, const QString &password);

    /// Fetches all VEVENTs in the given calendar collection.
    void fetchEvents(const QUrl &calendarUrl, const QString &username, const QString &password);

    /// Creates or updates the event resource identified by @p uid.
    void putEvent(const QUrl &calendarUrl,
                   const QString &uid,
                   const QByteArray &icsData,
                   const QString &etag,
                   const QString &username,
                   const QString &password);

    /// Deletes the event resource at @p href.
    void deleteEvent(const QUrl &href, const QString &etag, const QString &username, const QString &password);

Q_SIGNALS:
    void discoveryFinished(const QList<CalDavClient::CalendarInfo> &calendars, const QString &error);
    void eventsFetched(const QList<CalDavClient::RemoteEvent> &events, const QString &error);
    void eventPut(const QString &href, const QString &etag, const QString &error);
    void eventDeleted(const QString &error);

private:
    QNetworkReply *sendRequest(const QUrl &url,
                                const QByteArray &method,
                                const QByteArray &body,
                                const QString &username,
                                const QString &password,
                                const QMap<QByteArray, QByteArray> &extraHeaders = {});

    void requestCalendarHomeSet(const QUrl &principalUrl, const QUrl &serverUrl, const QString &username, const QString &password);
    void requestCalendarCollections(const QUrl &homeSetUrl, const QUrl &serverUrl, const QString &username, const QString &password);

    QNetworkAccessManager *m_nam;
};
