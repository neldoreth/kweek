#include "caldavclient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

namespace
{

/// One <D:response> entry from a WebDAV multistatus document.
struct PropfindResponse {
    QString href;
    QStringList resourceTypes;
    QMap<QString, QString> props;
};

/// Parses a WebDAV multistatus XML document into a flat list of responses.
/// For <current-user-principal> and <calendar-home-set> the inner <href> is
/// stored under the property's own (local) name.
QList<PropfindResponse> parseMultistatus(const QByteArray &xml)
{
    QList<PropfindResponse> result;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("response")) {
            continue;
        }

        PropfindResponse resp;
        while (!reader.atEnd() && !(reader.isEndElement() && reader.name() == QLatin1String("response"))) {
            reader.readNext();

            if (!reader.isStartElement()) {
                continue;
            }

            const QString name = reader.name().toString();

            if (name == QLatin1String("href") && resp.href.isEmpty()) {
                resp.href = reader.readElementText().trimmed();
            } else if (name == QLatin1String("resourcetype")) {
                while (!reader.atEnd() && !(reader.isEndElement() && reader.name() == QLatin1String("resourcetype"))) {
                    reader.readNext();
                    if (reader.isStartElement()) {
                        resp.resourceTypes.append(reader.name().toString());
                    }
                }
            } else if (name == QLatin1String("displayname") || name == QLatin1String("calendar-color")
                       || name == QLatin1String("getetag") || name == QLatin1String("calendar-data")) {
                resp.props.insert(name, reader.readElementText());
            } else if (name == QLatin1String("current-user-principal") || name == QLatin1String("calendar-home-set")) {
                while (!reader.atEnd() && !(reader.isEndElement() && reader.name() == name)) {
                    reader.readNext();
                    if (reader.isStartElement() && reader.name() == QLatin1String("href")) {
                        resp.props.insert(name, reader.readElementText().trimmed());
                    }
                }
            }
        }

        result.append(resp);
    }

    return result;
}

}

CalDavClient::CalDavClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QNetworkReply *CalDavClient::sendRequest(const QUrl &url,
                                          const QByteArray &method,
                                          const QByteArray &body,
                                          const QString &username,
                                          const QString &password,
                                          const QMap<QByteArray, QByteArray> &extraHeaders)
{
    QNetworkRequest request(url);
    const QByteArray credentials = (username + QStringLiteral(":") + password).toUtf8().toBase64();
    request.setRawHeader("Authorization", "Basic " + credentials);
    request.setRawHeader("Content-Type", "application/xml; charset=utf-8");

    for (auto it = extraHeaders.constBegin(); it != extraHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key(), it.value());
    }

    return m_nam->sendCustomRequest(request, method, body);
}

void CalDavClient::discoverCalendars(const QUrl &serverUrl, const QString &username, const QString &password)
{
    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "<D:prop><D:current-user-principal/></D:prop>"
        "</D:propfind>";

    QNetworkReply *reply = sendRequest(serverUrl, "PROPFIND", body, username, password, {{"Depth", "0"}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverUrl, username, password]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT discoveryFinished({}, reply->errorString());
            return;
        }

        const QByteArray data = reply->readAll();
        const QList<PropfindResponse> responses = parseMultistatus(data);

        QString principalHref;
        for (const PropfindResponse &resp : responses) {
            if (resp.props.contains(QStringLiteral("current-user-principal"))) {
                principalHref = resp.props.value(QStringLiteral("current-user-principal"));
                break;
            }
        }

        const QUrl principalUrl = principalHref.isEmpty() ? serverUrl : serverUrl.resolved(QUrl(principalHref));
        requestCalendarHomeSet(principalUrl, serverUrl, username, password);
    });
}

void CalDavClient::requestCalendarHomeSet(const QUrl &principalUrl, const QUrl &serverUrl, const QString &username, const QString &password)
{
    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
        "<D:prop><C:calendar-home-set/></D:prop>"
        "</D:propfind>";

    QNetworkReply *reply = sendRequest(principalUrl, "PROPFIND", body, username, password, {{"Depth", "0"}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverUrl, username, password]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT discoveryFinished({}, reply->errorString());
            return;
        }

        const QByteArray data = reply->readAll();
        const QList<PropfindResponse> responses = parseMultistatus(data);

        QString homeSetHref;
        for (const PropfindResponse &resp : responses) {
            if (resp.props.contains(QStringLiteral("calendar-home-set"))) {
                homeSetHref = resp.props.value(QStringLiteral("calendar-home-set"));
                break;
            }
        }

        if (homeSetHref.isEmpty()) {
            Q_EMIT discoveryFinished({}, QStringLiteral("Could not determine the calendar home set for this account."));
            return;
        }

        const QUrl homeSetUrl = serverUrl.resolved(QUrl(homeSetHref));
        requestCalendarCollections(homeSetUrl, serverUrl, username, password);
    });
}

void CalDavClient::requestCalendarCollections(const QUrl &homeSetUrl, const QUrl &serverUrl, const QString &username, const QString &password)
{
    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\" xmlns:CS=\"http://apple.com/ns/ical/\">"
        "<D:prop>"
        "<D:resourcetype/>"
        "<D:displayname/>"
        "<CS:calendar-color/>"
        "</D:prop>"
        "</D:propfind>";

    QNetworkReply *reply = sendRequest(homeSetUrl, "PROPFIND", body, username, password, {{"Depth", "1"}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT discoveryFinished({}, reply->errorString());
            return;
        }

        const QByteArray data = reply->readAll();
        const QList<PropfindResponse> responses = parseMultistatus(data);

        QList<CalendarInfo> calendars;
        for (const PropfindResponse &resp : responses) {
            if (!resp.resourceTypes.contains(QStringLiteral("calendar"))) {
                continue;
            }
            if (resp.resourceTypes.contains(QStringLiteral("schedule-inbox"))
                || resp.resourceTypes.contains(QStringLiteral("schedule-outbox"))) {
                continue;
            }

            CalendarInfo info;
            info.url = serverUrl.resolved(QUrl(resp.href)).toString();
            info.displayName = resp.props.value(QStringLiteral("displayname"));
            if (info.displayName.isEmpty()) {
                info.displayName = resp.href;
            }
            info.color = resp.props.value(QStringLiteral("calendar-color"));
            calendars.append(info);
        }

        Q_EMIT discoveryFinished(calendars, QString());
    });
}

void CalDavClient::fetchEvents(const QUrl &calendarUrl, const QString &username, const QString &password)
{
    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<C:calendar-query xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
        "<D:prop><D:getetag/><C:calendar-data/></D:prop>"
        "<C:filter><C:comp-filter name=\"VCALENDAR\"><C:comp-filter name=\"VEVENT\"/></C:comp-filter></C:filter>"
        "</C:calendar-query>";

    QNetworkReply *reply = sendRequest(calendarUrl, "REPORT", body, username, password, {{"Depth", "1"}});
    connect(reply, &QNetworkReply::finished, this, [this, reply, calendarUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT eventsFetched({}, reply->errorString());
            return;
        }

        const QByteArray data = reply->readAll();
        const QList<PropfindResponse> responses = parseMultistatus(data);

        QList<RemoteEvent> events;
        for (const PropfindResponse &resp : responses) {
            if (!resp.props.contains(QStringLiteral("calendar-data"))) {
                continue;
            }

            RemoteEvent ev;
            ev.href = calendarUrl.resolved(QUrl(resp.href)).toString();
            ev.etag = resp.props.value(QStringLiteral("getetag"));
            ev.data = resp.props.value(QStringLiteral("calendar-data"));
            events.append(ev);
        }

        Q_EMIT eventsFetched(events, QString());
    });
}

void CalDavClient::putEvent(const QUrl &calendarUrl,
                             const QString &uid,
                             const QByteArray &icsData,
                             const QString &etag,
                             const QString &username,
                             const QString &password)
{
    QUrl url = calendarUrl;
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    path += uid + QStringLiteral(".ics");
    url.setPath(path);

    QMap<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "text/calendar; charset=utf-8");
    if (!etag.isEmpty()) {
        headers.insert("If-Match", etag.toUtf8());
    }

    QNetworkReply *reply = sendRequest(url, "PUT", icsData, username, password, headers);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT eventPut(QString(), QString(), reply->errorString());
            return;
        }

        Q_EMIT eventPut(url.toString(), QString::fromUtf8(reply->rawHeader("ETag")), QString());
    });
}

void CalDavClient::deleteEvent(const QUrl &href, const QString &etag, const QString &username, const QString &password)
{
    QMap<QByteArray, QByteArray> headers;
    if (!etag.isEmpty()) {
        headers.insert("If-Match", etag.toUtf8());
    }

    QNetworkReply *reply = sendRequest(href, "DELETE", QByteArray(), username, password, headers);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QString error = reply->error() != QNetworkReply::NoError ? reply->errorString() : QString();
        Q_EMIT eventDeleted(error);
    });
}
