#include "googlecalendarclient.h"

#include "googleoauthconfig.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QUrlQuery>

namespace
{
constexpr auto kAuthorizationUrl = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr auto kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr auto kUserInfoUrl = "https://www.googleapis.com/oauth2/v3/userinfo";
constexpr auto kCalendarApiBase = "https://www.googleapis.com/calendar/v3";
constexpr int kGoneStatus = 410;
}

GoogleCalendarClient::GoogleCalendarClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void GoogleCalendarClient::authenticate()
{
    auto *replyHandler = new QOAuthHttpServerReplyHandler(this);

    m_flow = new QOAuth2AuthorizationCodeFlow(m_nam, this);
    m_flow->setReplyHandler(replyHandler);
    m_flow->setAuthorizationUrl(QUrl(QString::fromLatin1(kAuthorizationUrl)));
    m_flow->setTokenUrl(QUrl(QString::fromLatin1(kTokenUrl)));
    m_flow->setClientIdentifier(QString::fromLatin1(GoogleOAuthConfig::kClientId));
    m_flow->setClientIdentifierSharedKey(QString::fromLatin1(GoogleOAuthConfig::kClientSecret));
    m_flow->setRequestedScopeTokens({QByteArrayLiteral("https://www.googleapis.com/auth/calendar"), QByteArrayLiteral("email")});
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    m_flow->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            parameters->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            parameters->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
        }
    });

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        const QString refreshToken = m_flow->refreshToken();
        const QString accessToken = m_flow->token();

        QNetworkRequest request(QUrl(QString::fromLatin1(kUserInfoUrl)));
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, refreshToken]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT authenticated(QString(), QString(), reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            Q_EMIT authenticated(refreshToken, obj.value(QStringLiteral("email")).toString(), QString());
        });
    });

    m_flow->grant();
}

void GoogleCalendarClient::withAccessToken(const QString &refreshToken, const std::function<void(const QString &, const QString &)> &callback)
{
    QNetworkRequest request(QUrl(QString::fromLatin1(kTokenUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(GoogleOAuthConfig::kClientId));
    body.addQueryItem(QStringLiteral("client_secret"), QString::fromLatin1(GoogleOAuthConfig::kClientSecret));
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

    QNetworkReply *reply = m_nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(QString(), reply->errorString());
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString accessToken = obj.value(QStringLiteral("access_token")).toString();
        if (accessToken.isEmpty()) {
            callback(QString(), QStringLiteral("Google did not return an access token."));
            return;
        }

        callback(accessToken, QString());
    });
}

void GoogleCalendarClient::listCalendars(const QString &refreshToken)
{
    withAccessToken(refreshToken, [this](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT calendarsListed({}, error);
            return;
        }

        QNetworkRequest request(QUrl(QString::fromLatin1(kCalendarApiBase) + QStringLiteral("/users/me/calendarList")));
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT calendarsListed({}, reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QList<CalendarInfo> calendars;
            for (const QJsonValue &value : obj.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = value.toObject();
                CalendarInfo info;
                info.id = item.value(QStringLiteral("id")).toString();
                info.displayName = item.value(QStringLiteral("summary")).toString();
                info.color = item.value(QStringLiteral("backgroundColor")).toString();
                info.primary = item.value(QStringLiteral("primary")).toBool();
                calendars.append(info);
            }

            Q_EMIT calendarsListed(calendars, QString());
        });
    });
}

void GoogleCalendarClient::fetchEvents(const QString &refreshToken, const QString &calendarId, const QString &syncToken)
{
    withAccessToken(refreshToken, [this, calendarId, syncToken](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventsFetched({}, QString(), false, error);
            return;
        }

        QUrl url(QString::fromLatin1(kCalendarApiBase) + QStringLiteral("/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId)) + QStringLiteral("/events"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("singleEvents"), QStringLiteral("false"));
        if (!syncToken.isEmpty()) {
            query.addQueryItem(QStringLiteral("syncToken"), syncToken);
        }
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == kGoneStatus) {
                    Q_EMIT eventsFetched({}, QString(), true, QString());
                    return;
                }
                Q_EMIT eventsFetched({}, QString(), false, reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QList<RemoteEvent> events;
            for (const QJsonValue &value : obj.value(QStringLiteral("items")).toArray()) {
                const QJsonObject item = value.toObject();
                RemoteEvent ev;
                ev.id = item.value(QStringLiteral("id")).toString();
                ev.etag = item.value(QStringLiteral("etag")).toString();
                ev.json = item;
                events.append(ev);
            }

            Q_EMIT eventsFetched(events, obj.value(QStringLiteral("nextSyncToken")).toString(), false, QString());
        });
    });
}

void GoogleCalendarClient::putEvent(const QString &refreshToken, const QString &calendarId, const QString &googleEventId, const QJsonObject &json)
{
    withAccessToken(refreshToken, [this, calendarId, googleEventId, json](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventPut(QString(), QString(), error);
            return;
        }

        QString path = QString::fromLatin1(kCalendarApiBase) + QStringLiteral("/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId)) + QStringLiteral("/events");
        if (!googleEventId.isEmpty()) {
            path += QLatin1Char('/') + QString::fromUtf8(QUrl::toPercentEncoding(googleEventId));
        }

        QNetworkRequest request{QUrl(path)};
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = googleEventId.isEmpty() ? m_nam->post(request, body) : m_nam->sendCustomRequest(request, "PATCH", body);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT eventPut(QString(), QString(), reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            Q_EMIT eventPut(obj.value(QStringLiteral("id")).toString(), obj.value(QStringLiteral("etag")).toString(), QString());
        });
    });
}

void GoogleCalendarClient::deleteEvent(const QString &refreshToken, const QString &calendarId, const QString &googleEventId)
{
    withAccessToken(refreshToken, [this, calendarId, googleEventId](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventDeleted(error);
            return;
        }

        const QString path = QString::fromLatin1(kCalendarApiBase) + QStringLiteral("/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId))
            + QStringLiteral("/events/") + QString::fromUtf8(QUrl::toPercentEncoding(googleEventId));

        QNetworkRequest request{QUrl(path)};
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->sendCustomRequest(request, "DELETE");
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            const bool gone = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == kGoneStatus;
            const QString error = (reply->error() != QNetworkReply::NoError && !gone) ? reply->errorString() : QString();
            Q_EMIT eventDeleted(error);
        });
    });
}
