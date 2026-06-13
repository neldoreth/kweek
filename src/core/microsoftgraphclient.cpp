#include "microsoftgraphclient.h"

#include "microsoftoauthconfig.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QUrlQuery>

namespace
{
constexpr auto kAuthorizationUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize";
constexpr auto kTokenUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/token";
constexpr auto kMeUrl = "https://graph.microsoft.com/v1.0/me";
constexpr auto kGraphApiBase = "https://graph.microsoft.com/v1.0";
constexpr int kNotFoundStatus = 404;
constexpr int kGoneStatus = 410;

/// Maps Microsoft Graph's named calendar colors to an approximate hex value.
/// "auto" and unrecognized names return an empty string (caller falls back
/// to the app's default calendar color).
QString graphColorToHex(const QString &color)
{
    static const QMap<QString, QString> colors{
        {QStringLiteral("lightBlue"), QStringLiteral("#9BD0F5")},
        {QStringLiteral("lightGreen"), QStringLiteral("#A0E5A0")},
        {QStringLiteral("lightOrange"), QStringLiteral("#F5CBA7")},
        {QStringLiteral("lightGray"), QStringLiteral("#D3D3D3")},
        {QStringLiteral("lightYellow"), QStringLiteral("#F5F0A7")},
        {QStringLiteral("lightTeal"), QStringLiteral("#A7E5E0")},
        {QStringLiteral("lightPink"), QStringLiteral("#F5A7D3")},
        {QStringLiteral("lightBrown"), QStringLiteral("#D2B48C")},
        {QStringLiteral("lightRed"), QStringLiteral("#F5A7A7")},
        {QStringLiteral("maxColor"), QStringLiteral("#7B61FF")},
    };
    return colors.value(color);
}

}

MicrosoftGraphClient::MicrosoftGraphClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void MicrosoftGraphClient::authenticate()
{
    auto *replyHandler = new QOAuthHttpServerReplyHandler(this);

    m_flow = new QOAuth2AuthorizationCodeFlow(m_nam, this);
    m_flow->setReplyHandler(replyHandler);
    m_flow->setAuthorizationUrl(QUrl(QString::fromLatin1(kAuthorizationUrl)));
    m_flow->setTokenUrl(QUrl(QString::fromLatin1(kTokenUrl)));
    m_flow->setClientIdentifier(QString::fromLatin1(MicrosoftOAuthConfig::kClientId));
    m_flow->setRequestedScopeTokens({
        QByteArrayLiteral("https://graph.microsoft.com/Calendars.ReadWrite"),
        QByteArrayLiteral("https://graph.microsoft.com/User.Read"),
        QByteArrayLiteral("offline_access"),
        QByteArrayLiteral("openid"),
        QByteArrayLiteral("email"),
    });
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this, [](const QUrl &url) {
        QDesktopServices::openUrl(url);
    });

    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, [this](const QString &error, const QString &errorDescription, const QUrl &uri) {
        qWarning() << "MicrosoftGraphClient: OAuth error" << error << errorDescription << uri;
        Q_EMIT authenticated(QString(), QString(), error + QStringLiteral(": ") + errorDescription);
    });

    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        qWarning() << "MicrosoftGraphClient: OAuth request failed" << static_cast<int>(error);
        Q_EMIT authenticated(QString(), QString(), QStringLiteral("OAuth request failed (%1)").arg(static_cast<int>(error)));
    });

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        const QString refreshToken = m_flow->refreshToken();
        const QString accessToken = m_flow->token();

        QNetworkRequest request(QUrl(QString::fromLatin1(kMeUrl)));
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, refreshToken]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT authenticated(QString(), QString(), reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            QString email = obj.value(QStringLiteral("mail")).toString();
            if (email.isEmpty()) {
                email = obj.value(QStringLiteral("userPrincipalName")).toString();
            }
            Q_EMIT authenticated(refreshToken, email, QString());
        });
    });

    m_flow->grant();
}

void MicrosoftGraphClient::withAccessToken(const QString &refreshToken, const std::function<void(const QString &, const QString &)> &callback)
{
    QNetworkRequest request(QUrl(QString::fromLatin1(kTokenUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(MicrosoftOAuthConfig::kClientId));
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("scope"),
                       QStringLiteral("https://graph.microsoft.com/Calendars.ReadWrite https://graph.microsoft.com/User.Read offline_access openid email"));

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
            callback(QString(), QStringLiteral("Microsoft did not return an access token."));
            return;
        }

        callback(accessToken, QString());
    });
}

void MicrosoftGraphClient::listCalendars(const QString &refreshToken)
{
    withAccessToken(refreshToken, [this](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT calendarsListed({}, error);
            return;
        }

        QNetworkRequest request(QUrl(QString::fromLatin1(kGraphApiBase) + QStringLiteral("/me/calendars")));
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
            for (const QJsonValue &value : obj.value(QStringLiteral("value")).toArray()) {
                const QJsonObject item = value.toObject();
                CalendarInfo info;
                info.id = item.value(QStringLiteral("id")).toString();
                info.displayName = item.value(QStringLiteral("name")).toString();
                info.color = graphColorToHex(item.value(QStringLiteral("color")).toString());
                info.primary = item.value(QStringLiteral("isDefaultCalendar")).toBool();
                calendars.append(info);
            }

            Q_EMIT calendarsListed(calendars, QString());
        });
    });
}

void MicrosoftGraphClient::fetchEvents(const QString &refreshToken, const QString &calendarId, const QString &deltaLink)
{
    withAccessToken(refreshToken, [this, calendarId, deltaLink](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventsFetched({}, QString(), false, error);
            return;
        }

        fetchEventsPage(accessToken, calendarId, deltaLink, {});
    });
}

void MicrosoftGraphClient::fetchEventsPage(const QString &accessToken, const QString &calendarId, const QString &deltaLink, QList<RemoteEvent> accumulated)
{
    QUrl url;
    if (deltaLink.isEmpty()) {
        url = QUrl(QString::fromLatin1(kGraphApiBase) + QStringLiteral("/me/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId)) + QStringLiteral("/events/delta"));
    } else {
        url = QUrl(deltaLink);
    }

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setRawHeader("Prefer", "outlook.timezone=\"UTC\"");

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, accessToken, calendarId, accumulated]() mutable {
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
        for (const QJsonValue &value : obj.value(QStringLiteral("value")).toArray()) {
            const QJsonObject item = value.toObject();
            RemoteEvent ev;
            ev.id = item.value(QStringLiteral("id")).toString();
            ev.etag = item.value(QStringLiteral("@odata.etag")).toString();
            ev.json = item;
            ev.removed = item.contains(QStringLiteral("@removed"));
            accumulated.append(ev);
        }

        const QString nextLink = obj.value(QStringLiteral("@odata.nextLink")).toString();
        if (!nextLink.isEmpty()) {
            fetchEventsPage(accessToken, calendarId, nextLink, accumulated);
            return;
        }

        Q_EMIT eventsFetched(accumulated, obj.value(QStringLiteral("@odata.deltaLink")).toString(), false, QString());
    });
}

void MicrosoftGraphClient::putEvent(const QString &refreshToken, const QString &calendarId, const QString &eventId, const QJsonObject &json)
{
    withAccessToken(refreshToken, [this, calendarId, eventId, json](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventPut(QString(), QString(), error);
            return;
        }

        QString path = QString::fromLatin1(kGraphApiBase) + QStringLiteral("/me/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId)) + QStringLiteral("/events");
        if (!eventId.isEmpty()) {
            path += QLatin1Char('/') + QString::fromUtf8(QUrl::toPercentEncoding(eventId));
        }

        QNetworkRequest request{QUrl(path)};
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
        request.setRawHeader("Prefer", "outlook.timezone=\"UTC\"");
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QNetworkReply *reply = eventId.isEmpty() ? m_nam->post(request, body) : m_nam->sendCustomRequest(request, "PATCH", body);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT eventPut(QString(), QString(), reply->errorString());
                return;
            }

            const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
            Q_EMIT eventPut(obj.value(QStringLiteral("id")).toString(), obj.value(QStringLiteral("@odata.etag")).toString(), QString());
        });
    });
}

void MicrosoftGraphClient::deleteEvent(const QString &refreshToken, const QString &calendarId, const QString &eventId)
{
    withAccessToken(refreshToken, [this, calendarId, eventId](const QString &accessToken, const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT eventDeleted(error);
            return;
        }

        const QString path = QString::fromLatin1(kGraphApiBase) + QStringLiteral("/me/calendars/") + QString::fromUtf8(QUrl::toPercentEncoding(calendarId))
            + QStringLiteral("/events/") + QString::fromUtf8(QUrl::toPercentEncoding(eventId));

        QNetworkRequest request{QUrl(path)};
        request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());

        QNetworkReply *reply = m_nam->sendCustomRequest(request, "DELETE");
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            const bool notFound = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == kNotFoundStatus;
            const QString error = (reply->error() != QNetworkReply::NoError && !notFound) ? reply->errorString() : QString();
            Q_EMIT eventDeleted(error);
        });
    });
}
