#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;

/**
 * Client for the Microsoft Graph API (v1.0) calendar endpoints, authenticating
 * via OAuth2 with PKCE (QOAuth2AuthorizationCodeFlow + a local loopback
 * redirect handler), mirroring GoogleCalendarClient.
 *
 * After the initial interactive authenticate(), all other operations take a
 * stored refresh token and exchange it for a fresh access token before each
 * call (see withAccessToken()), since QOAuth2AuthorizationCodeFlow is not
 * reused for background sync.
 *
 * All requests are sent with "Prefer: outlook.timezone=\"UTC\"" so that
 * event start/end/originalStart dateTimes are always in UTC, avoiding the
 * need to map Windows time zone names to IANA ones.
 */
class MicrosoftGraphClient : public QObject
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
        bool removed = false;
    };

    explicit MicrosoftGraphClient(QObject *parent = nullptr);

    /// Runs the interactive OAuth2 PKCE flow via the system browser.
    void authenticate();

    /// Lists the calendars on the user's Microsoft account.
    void listCalendars(const QString &refreshToken);

    /// Fetches events for @p calendarId via a delta query, incrementally if @p deltaLink is non-empty.
    void fetchEvents(const QString &refreshToken, const QString &calendarId, const QString &deltaLink);

    /// Creates (empty @p eventId) or updates an event.
    void putEvent(const QString &refreshToken, const QString &calendarId, const QString &eventId, const QJsonObject &json);

    /// Deletes the event identified by @p eventId.
    void deleteEvent(const QString &refreshToken, const QString &calendarId, const QString &eventId);

Q_SIGNALS:
    void authenticated(const QString &refreshToken, const QString &email, const QString &error);
    void calendarsListed(const QList<MicrosoftGraphClient::CalendarInfo> &calendars, const QString &error);
    void eventsFetched(const QList<MicrosoftGraphClient::RemoteEvent> &events, const QString &nextDeltaLink, bool deltaInvalid, const QString &error);
    void eventPut(const QString &eventId, const QString &etag, const QString &error);
    void eventDeleted(const QString &error);

private:
    /// Exchanges @p refreshToken for a fresh access token, then invokes @p callback(accessToken, error).
    void withAccessToken(const QString &refreshToken, const std::function<void(const QString &, const QString &)> &callback);

    /// Fetches one page of the delta query, recursing via "@odata.nextLink" until "@odata.deltaLink" is returned, then emits eventsFetched().
    void fetchEventsPage(const QString &accessToken, const QString &calendarId, const QString &deltaLink, QList<RemoteEvent> accumulated);

    QNetworkAccessManager *m_nam;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
};
