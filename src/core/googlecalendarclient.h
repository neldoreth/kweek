#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;

/**
 * Client for the Google Calendar API (v3), authenticating via OAuth2 with
 * PKCE (QOAuth2AuthorizationCodeFlow + a local loopback redirect handler).
 *
 * After the initial interactive authenticate(), all other operations take a
 * stored refresh token and exchange it for a fresh access token before each
 * call (see withAccessToken()), since QOAuth2AuthorizationCodeFlow is not
 * reused for background sync.
 */
class GoogleCalendarClient : public QObject
{
    Q_OBJECT

public:
    struct CalendarInfo {
        QString id;
        QString displayName;
        QString color;
        bool primary = false;
    };

    struct RemoteEvent {
        QString id;
        QString etag;
        QJsonObject json;
    };

    explicit GoogleCalendarClient(QObject *parent = nullptr);

    /// Runs the interactive OAuth2 PKCE flow via the system browser.
    void authenticate();

    /// Lists the calendars on the user's Google account.
    void listCalendars(const QString &refreshToken);

    /// Fetches events for @p calendarId, incrementally if @p syncToken is non-empty.
    void fetchEvents(const QString &refreshToken, const QString &calendarId, const QString &syncToken);

    /// Creates (empty @p googleEventId) or updates an event.
    void putEvent(const QString &refreshToken, const QString &calendarId, const QString &googleEventId, const QJsonObject &json);

    /// Deletes the event identified by @p googleEventId.
    void deleteEvent(const QString &refreshToken, const QString &calendarId, const QString &googleEventId);

Q_SIGNALS:
    void authenticated(const QString &refreshToken, const QString &email, const QString &error);
    void calendarsListed(const QList<GoogleCalendarClient::CalendarInfo> &calendars, const QString &error);
    void eventsFetched(const QList<GoogleCalendarClient::RemoteEvent> &events, const QString &nextSyncToken, bool syncTokenInvalid, const QString &error);
    void eventPut(const QString &eventId, const QString &etag, const QString &error);
    void eventDeleted(const QString &error);

private:
    /// Exchanges @p refreshToken for a fresh access token, then invokes @p callback(accessToken, error).
    void withAccessToken(const QString &refreshToken, const std::function<void(const QString &, const QString &)> &callback);

    /// Fetches one page of events, recursing via "pageToken" until all pages are collected, then emits eventsFetched().
    void fetchEventsPage(const QString &accessToken, const QString &calendarId, const QString &syncToken, const QString &pageToken, QList<RemoteEvent> accumulated);

    QNetworkAccessManager *m_nam;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
};
