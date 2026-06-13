#include "calendarmanager.h"

#include "caldavclient.h"
#include "credentialstore.h"
#include "googlecalendarclient.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/CalFormat>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>
#include <KLocalizedString>

using namespace KCalendarCore;

namespace
{
constexpr auto kPropertyApp = "KWEEK";
constexpr auto kColorProperty = "COLOR";
constexpr auto kDefaultCalendarId = "default";
constexpr auto kDefaultCalendarColor = "#7B61FF";
constexpr auto kTypeLocal = "local";
constexpr auto kTypeCalDav = "caldav";
constexpr auto kTypeGoogle = "google";

void applyRecurrence(const Event::Ptr &event, const QString &recurrence)
{
    event->recurrence()->clear();

    if (recurrence == QLatin1String("daily")) {
        event->recurrence()->setDaily(1);
    } else if (recurrence == QLatin1String("weekly")) {
        event->recurrence()->setWeekly(1);
    } else if (recurrence == QLatin1String("monthly")) {
        event->recurrence()->setMonthly(1);
    } else if (recurrence == QLatin1String("yearly")) {
        event->recurrence()->setYearly(1);
    }
    // "none" (or anything else) leaves the recurrence cleared.
}

void applyReminder(const Event::Ptr &event, int reminderMinutes)
{
    event->clearAlarms();

    if (reminderMinutes < 0) {
        return;
    }

    Alarm::Ptr alarm = event->newAlarm();
    alarm->setType(Alarm::Display);
    alarm->setDisplayAlarm(event->summary());
    alarm->setStartOffset(Duration(-reminderMinutes * 60));
    alarm->setEnabled(true);
}

void applyEventFields(const Event::Ptr &event,
                       const QString &summary,
                       const QString &description,
                       const QString &location,
                       const QDateTime &start,
                       const QDateTime &end,
                       bool allDay,
                       const QString &color,
                       const QString &recurrence,
                       int reminderMinutes,
                       bool busy)
{
    event->setSummary(summary);
    event->setDescription(description);
    event->setLocation(location);
    event->setAllDay(allDay);
    event->setDtStart(start);
    event->setDtEnd(end);
    event->setTransparency(busy ? Event::Opaque : Event::Transparent);
    event->setCustomProperty(kPropertyApp, kColorProperty, color);

    applyRecurrence(event, recurrence);
    applyReminder(event, reminderMinutes);
}

/// Maps a KCalendarCore recurrence type to the FREQ value used in RRULE strings.
QString rruleFreqForRecurrenceType(int recurrenceType)
{
    switch (recurrenceType) {
    case RecurrenceRule::rDaily:
        return QStringLiteral("DAILY");
    case RecurrenceRule::rWeekly:
        return QStringLiteral("WEEKLY");
    case RecurrenceRule::rMonthly:
        return QStringLiteral("MONTHLY");
    case RecurrenceRule::rYearly:
        return QStringLiteral("YEARLY");
    default:
        return QString();
    }
}

/// Extracts the FREQ value from a "RRULE:FREQ=...;..." string, or an empty string.
QString rruleFreq(const QString &rrule)
{
    const QString rule = rrule.startsWith(QLatin1String("RRULE:")) ? rrule.mid(6) : rrule;
    for (const QString &part : rule.split(QLatin1Char(';'))) {
        if (part.startsWith(QLatin1String("FREQ="))) {
            return part.mid(5);
        }
    }
    return QString();
}

/// Extracts the UNTIL value from a "RRULE:FREQ=...;..." string as a QDateTime, or an invalid QDateTime.
QDateTime rruleUntil(const QString &rrule)
{
    const QString rule = rrule.startsWith(QLatin1String("RRULE:")) ? rrule.mid(6) : rrule;
    for (const QString &part : rule.split(QLatin1Char(';'))) {
        if (!part.startsWith(QLatin1String("UNTIL="))) {
            continue;
        }
        const QString value = part.mid(6);
        if (value.endsWith(QLatin1Char('Z'))) {
            QDateTime dt = QDateTime::fromString(value.chopped(1), QStringLiteral("yyyyMMddTHHmmss"));
            dt.setTimeZone(QTimeZone::utc());
            return dt;
        }
        if (value.contains(QLatin1Char('T'))) {
            return QDateTime::fromString(value, QStringLiteral("yyyyMMddTHHmmss"));
        }
        return QDateTime(QDate::fromString(value, QStringLiteral("yyyyMMdd")), QTime(23, 59, 59));
    }
    return QDateTime();
}

/// Extracts the COUNT value from a "RRULE:FREQ=...;..." string, or -1 if not present.
int rruleCount(const QString &rrule)
{
    const QString rule = rrule.startsWith(QLatin1String("RRULE:")) ? rrule.mid(6) : rrule;
    for (const QString &part : rule.split(QLatin1Char(';'))) {
        if (part.startsWith(QLatin1String("COUNT="))) {
            return part.mid(6).toInt();
        }
    }
    return -1;
}

/// Converts a Google Calendar API event resource into a KCalendarCore event.
/// The event's UID is set to the Google event id.
Event::Ptr googleJsonToEvent(const QJsonObject &json)
{
    Event::Ptr event(new Event);
    event->setUid(json.value(QStringLiteral("id")).toString());
    event->setSummary(json.value(QStringLiteral("summary")).toString());
    event->setDescription(json.value(QStringLiteral("description")).toString());
    event->setLocation(json.value(QStringLiteral("location")).toString());

    const QJsonObject start = json.value(QStringLiteral("start")).toObject();
    const QJsonObject end = json.value(QStringLiteral("end")).toObject();
    const bool allDay = start.contains(QStringLiteral("date"));
    event->setAllDay(allDay);

    if (allDay) {
        event->setDtStart(QDateTime(QDate::fromString(start.value(QStringLiteral("date")).toString(), Qt::ISODate), QTime(0, 0)));
        event->setDtEnd(QDateTime(QDate::fromString(end.value(QStringLiteral("date")).toString(), Qt::ISODate), QTime(0, 0)));
    } else {
        QDateTime dtStart = QDateTime::fromString(start.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
        QDateTime dtEnd = QDateTime::fromString(end.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
        const QTimeZone startZone(start.value(QStringLiteral("timeZone")).toString().toUtf8());
        const QTimeZone endZone(end.value(QStringLiteral("timeZone")).toString().toUtf8());
        if (startZone.isValid()) {
            dtStart = dtStart.toTimeZone(startZone);
        }
        if (endZone.isValid()) {
            dtEnd = dtEnd.toTimeZone(endZone);
        }
        event->setDtStart(dtStart);
        event->setDtEnd(dtEnd);
    }

    const QString transparency = json.value(QStringLiteral("transparency")).toString();
    event->setTransparency(transparency == QLatin1String("transparent") ? Event::Transparent : Event::Opaque);

    event->recurrence()->clear();
    for (const QJsonValue &value : json.value(QStringLiteral("recurrence")).toArray()) {
        const QString rrule = value.toString();
        const QString freq = rruleFreq(rrule);
        if (freq == QLatin1String("DAILY")) {
            event->recurrence()->setDaily(1);
        } else if (freq == QLatin1String("WEEKLY")) {
            event->recurrence()->setWeekly(1);
        } else if (freq == QLatin1String("MONTHLY")) {
            event->recurrence()->setMonthly(1);
        } else if (freq == QLatin1String("YEARLY")) {
            event->recurrence()->setYearly(1);
        } else {
            break;
        }

        const QDateTime until = rruleUntil(rrule);
        const int count = rruleCount(rrule);
        if (until.isValid()) {
            event->recurrence()->setEndDateTime(until);
        } else if (count > 0) {
            event->recurrence()->setDuration(count);
        }
        break;
    }

    event->clearAlarms();
    const QJsonArray overrides = json.value(QStringLiteral("reminders")).toObject().value(QStringLiteral("overrides")).toArray();
    for (const QJsonValue &value : overrides) {
        const QJsonObject override = value.toObject();
        if (override.value(QStringLiteral("method")).toString() != QLatin1String("popup")) {
            continue;
        }
        Alarm::Ptr alarm = event->newAlarm();
        alarm->setType(Alarm::Display);
        alarm->setDisplayAlarm(event->summary());
        alarm->setStartOffset(Duration(-override.value(QStringLiteral("minutes")).toInt() * 60));
        alarm->setEnabled(true);
        break;
    }

    return event;
}

/// Returns the QDateTime represented by a Google "originalStartTime"-shaped object
/// ({"date": ...} or {"dateTime": ..., "timeZone": ...}).
QDateTime googleOriginalStartTime(const QJsonObject &json)
{
    const QJsonObject original = json.value(QStringLiteral("originalStartTime")).toObject();
    if (original.contains(QStringLiteral("date"))) {
        return QDateTime(QDate::fromString(original.value(QStringLiteral("date")).toString(), Qt::ISODate), QTime(0, 0));
    }

    QDateTime dt = QDateTime::fromString(original.value(QStringLiteral("dateTime")).toString(), Qt::ISODate);
    const QTimeZone zone(original.value(QStringLiteral("timeZone")).toString().toUtf8());
    if (zone.isValid()) {
        dt = dt.toTimeZone(zone);
    }
    return dt;
}

/// Builds a KCalendarCore exception event from a Google "instance" item that has a
/// "recurringEventId" (a modified occurrence of a recurring event). The returned event
/// shares its uid with the master series and has recurrenceId() set to the original
/// occurrence's start (from "originalStartTime").
Event::Ptr googleJsonToExceptionEvent(const QJsonObject &json)
{
    Event::Ptr event = googleJsonToEvent(json);
    event->setUid(json.value(QStringLiteral("recurringEventId")).toString());
    event->recurrence()->clear();
    event->setRecurrenceId(googleOriginalStartTime(json));
    return event;
}

/// Converts a KCalendarCore event into a Google Calendar API event resource (without "id").
QJsonObject eventToGoogleJson(const Event::Ptr &event)
{
    QJsonObject json;
    json.insert(QStringLiteral("summary"), event->summary());
    json.insert(QStringLiteral("description"), event->description());
    json.insert(QStringLiteral("location"), event->location());

    QJsonObject start;
    QJsonObject end;
    if (event->allDay()) {
        start.insert(QStringLiteral("date"), event->dtStart().date().toString(Qt::ISODate));
        end.insert(QStringLiteral("date"), event->dtEnd().date().addDays(1).toString(Qt::ISODate));
    } else {
        start.insert(QStringLiteral("dateTime"), event->dtStart().toString(Qt::ISODate));
        start.insert(QStringLiteral("timeZone"), QString::fromUtf8(event->dtStart().timeZone().id()));
        end.insert(QStringLiteral("dateTime"), event->dtEnd().toString(Qt::ISODate));
        end.insert(QStringLiteral("timeZone"), QString::fromUtf8(event->dtEnd().timeZone().id()));
    }
    json.insert(QStringLiteral("start"), start);
    json.insert(QStringLiteral("end"), end);

    json.insert(QStringLiteral("transparency"), event->transparency() == Event::Transparent ? QStringLiteral("transparent") : QStringLiteral("opaque"));

    if (event->recurs()) {
        const QString freq = rruleFreqForRecurrenceType(event->recurrence()->recurrenceType());
        if (!freq.isEmpty()) {
            QString rrule = QStringLiteral("RRULE:FREQ=") + freq;
            const QDateTime until = event->recurrence()->endDateTime();
            const int duration = event->recurrence()->duration();
            if (until.isValid()) {
                rrule += QStringLiteral(";UNTIL=") + until.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
            } else if (duration > 0) {
                rrule += QStringLiteral(";COUNT=") + QString::number(duration);
            }
            json.insert(QStringLiteral("recurrence"), QJsonArray{rrule});
        }
    }

    QJsonObject reminders;
    reminders.insert(QStringLiteral("useDefault"), false);
    const Alarm::List alarms = event->alarms();
    if (!alarms.isEmpty()) {
        reminders.insert(QStringLiteral("overrides"), QJsonArray{QJsonObject{
            {QStringLiteral("method"), QStringLiteral("popup")},
            {QStringLiteral("minutes"), int(-alarms.first()->startOffset().asSeconds() / 60)},
        }});
    }
    json.insert(QStringLiteral("reminders"), reminders);

    return json;
}

}

CalendarManager::CalendarManager(QObject *parent)
    : QObject(parent)
{
    loadAccounts();
    loadCalendars();
}

QString CalendarManager::calendarsMetaPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/calendars.json");
}

QString CalendarManager::accountsMetaPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/accounts.json");
}

QString CalendarManager::icsPathFor(const QString &id) const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    if (id == QLatin1String(kDefaultCalendarId)) {
        return dataDir + QStringLiteral("/calendar.ics");
    }

    QDir().mkpath(dataDir + QStringLiteral("/calendars"));
    return dataDir + QStringLiteral("/calendars/") + id + QStringLiteral(".ics");
}

QString CalendarManager::syncStatePath(const QString &id) const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir + QStringLiteral("/calendars"));
    return dataDir + QStringLiteral("/calendars/") + id + QStringLiteral(".sync.json");
}

void CalendarManager::loadSyncState(LocalCalendar &entry)
{
    QFile file(syncStatePath(entry.id));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject items = obj.value(QStringLiteral("items")).toObject();
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        const QJsonObject item = it.value().toObject();
        entry.syncItems.insert(it.key(), qMakePair(item.value(QStringLiteral("href")).toString(),
                                                     item.value(QStringLiteral("etag")).toString()));
    }
    entry.syncToken = obj.value(QStringLiteral("syncToken")).toString();

    entry.pendingPush.clear();
    for (const QJsonValue &value : obj.value(QStringLiteral("pendingPush")).toArray()) {
        entry.pendingPush.insert(value.toString());
    }
}

void CalendarManager::saveSyncState(const LocalCalendar &entry)
{
    QJsonObject items;
    for (auto it = entry.syncItems.constBegin(); it != entry.syncItems.constEnd(); ++it) {
        items.insert(it.key(), QJsonObject{
            {QStringLiteral("href"), it.value().first},
            {QStringLiteral("etag"), it.value().second},
        });
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("items"), items);
    if (entry.type == QLatin1String(kTypeGoogle)) {
        obj.insert(QStringLiteral("syncToken"), entry.syncToken);
    }

    QJsonArray pendingPush;
    for (const QString &uid : entry.pendingPush) {
        pendingPush.append(uid);
    }
    obj.insert(QStringLiteral("pendingPush"), pendingPush);

    QFile file(syncStatePath(entry.id));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(obj).toJson());
    }
}

void CalendarManager::loadAccounts()
{
    QFile file(accountsMetaPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        Account account;
        account.id = obj.value(QStringLiteral("id")).toString();
        account.type = obj.value(QStringLiteral("type")).toString(QString::fromLatin1(kTypeCalDav));
        account.serverUrl = obj.value(QStringLiteral("serverUrl")).toString();
        account.username = obj.value(QStringLiteral("username")).toString();
        m_accounts.append(account);
    }
}

void CalendarManager::saveAccountsMeta()
{
    QJsonArray array;
    for (const Account &account : m_accounts) {
        array.append(QJsonObject{
            {QStringLiteral("id"), account.id},
            {QStringLiteral("type"), account.type},
            {QStringLiteral("serverUrl"), account.serverUrl},
            {QStringLiteral("username"), account.username},
        });
    }

    QFile file(accountsMetaPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(array).toJson());
    }
}

void CalendarManager::loadCalendars()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    QJsonArray array;
    QFile metaFile(calendarsMetaPath());
    if (metaFile.open(QIODevice::ReadOnly)) {
        array = QJsonDocument::fromJson(metaFile.readAll()).array();
    }

    if (array.isEmpty()) {
        array.append(QJsonObject{
            {QStringLiteral("id"), QString::fromLatin1(kDefaultCalendarId)},
            {QStringLiteral("name"), i18nc("@item default calendar name", "Personal")},
            {QStringLiteral("color"), QString::fromLatin1(kDefaultCalendarColor)},
            {QStringLiteral("visible"), true},
        });
    }

    m_calendars.clear();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();

        LocalCalendar entry;
        entry.id = obj.value(QStringLiteral("id")).toString();
        entry.name = obj.value(QStringLiteral("name")).toString();
        entry.color = obj.value(QStringLiteral("color")).toString();
        entry.visible = obj.value(QStringLiteral("visible")).toBool(true);
        entry.type = obj.value(QStringLiteral("type")).toString(QString::fromLatin1(kTypeLocal));
        entry.accountId = obj.value(QStringLiteral("accountId")).toString();
        entry.remoteUrl = obj.value(QStringLiteral("remoteUrl")).toString();
        entry.calendar = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::systemTimeZone()));
        entry.storage = FileStorage::Ptr(new FileStorage(entry.calendar, icsPathFor(entry.id)));
        entry.storage->load();

        if (entry.type == QLatin1String(kTypeCalDav) || entry.type == QLatin1String(kTypeGoogle)) {
            loadSyncState(entry);
        }

        m_calendars.append(entry);
    }

    saveCalendarsMeta();
}

void CalendarManager::saveCalendarsMeta()
{
    QJsonArray array;
    for (const LocalCalendar &entry : m_calendars) {
        QJsonObject obj{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("color"), entry.color},
            {QStringLiteral("visible"), entry.visible},
            {QStringLiteral("type"), entry.type},
        };
        if (entry.type == QLatin1String(kTypeCalDav) || entry.type == QLatin1String(kTypeGoogle)) {
            obj.insert(QStringLiteral("accountId"), entry.accountId);
            obj.insert(QStringLiteral("remoteUrl"), entry.remoteUrl);
        }
        array.append(obj);
    }

    QFile metaFile(calendarsMetaPath());
    if (metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        metaFile.write(QJsonDocument(array).toJson());
    }
}

const QList<CalendarManager::LocalCalendar> &CalendarManager::localCalendars() const
{
    return m_calendars;
}

QVariantList CalendarManager::calendars() const
{
    QVariantList list;
    for (const LocalCalendar &entry : m_calendars) {
        list.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("color"), entry.color},
            {QStringLiteral("visible"), entry.visible},
            {QStringLiteral("type"), entry.type},
            {QStringLiteral("accountId"), entry.accountId},
        });
    }
    return list;
}

CalendarManager::LocalCalendar *CalendarManager::findCalendarById(const QString &id)
{
    for (LocalCalendar &entry : m_calendars) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const CalendarManager::LocalCalendar *CalendarManager::findCalendarById(const QString &id) const
{
    for (const LocalCalendar &entry : m_calendars) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

CalendarManager::LocalCalendar *CalendarManager::findCalendarForEvent(const QString &uid, Event::Ptr *eventOut)
{
    for (LocalCalendar &entry : m_calendars) {
        const Event::Ptr event = entry.calendar->event(uid);
        if (event) {
            if (eventOut) {
                *eventOut = event;
            }
            return &entry;
        }
    }
    return nullptr;
}

void CalendarManager::markPendingPush(LocalCalendar &entry, const QString &uid)
{
    if (entry.type != QLatin1String(kTypeCalDav) && entry.type != QLatin1String(kTypeGoogle)) {
        return;
    }
    entry.pendingPush.insert(uid);
    saveSyncState(entry);
}

void CalendarManager::retryPendingPushes(LocalCalendar &entry)
{
    const QSet<QString> pending = entry.pendingPush;
    for (const QString &uid : pending) {
        const Event::Ptr event = entry.calendar->event(uid);
        if (!event) {
            // Locally deleted while offline; pushDelete() already handled the remote side.
            entry.pendingPush.remove(uid);
            continue;
        }
        pushEvent(entry, event);
    }
    saveSyncState(entry);
}

void CalendarManager::pushEvent(LocalCalendar &entry, const Event::Ptr &event)
{
    if (entry.type == QLatin1String(kTypeGoogle)) {
        QString email;
        QString refreshToken;
        if (!CredentialStore::readGoogleTokens(entry.accountId, email, refreshToken)) {
            Q_EMIT syncError(entry.id, i18nc("@info", "No stored credentials for this account."));
            return;
        }

        const QJsonObject json = eventToGoogleJson(event);
        QString googleEventId;
        const auto syncIt = entry.syncItems.constFind(event->uid());
        if (syncIt != entry.syncItems.constEnd()) {
            googleEventId = syncIt.value().first;
        }

        auto *client = new GoogleCalendarClient(this);
        const QString calendarId = entry.id;
        const QString uid = event->uid();
        const QString remoteCalendarId = entry.remoteUrl;

        connect(client, &GoogleCalendarClient::eventPut, this,
                [this, client, calendarId, uid](const QString &eventId, const QString &etag, const QString &error) {
                    client->deleteLater();

                    if (!error.isEmpty()) {
                        Q_EMIT syncError(calendarId, error);
                        return;
                    }

                    LocalCalendar *target = findCalendarById(calendarId);
                    if (!target) {
                        return;
                    }

                    // Newly created events get a server-assigned id; rename the local UID to match.
                    if (uid != eventId) {
                        const Event::Ptr existing = target->calendar->event(uid);
                        if (existing) {
                            const Event::Ptr clone(existing->clone());
                            clone->setUid(eventId);
                            target->calendar->deleteEvent(existing);
                            target->calendar->addEvent(clone);
                            target->storage->save();
                            Q_EMIT calendarChanged();
                        }
                        target->syncItems.remove(uid);
                        target->pendingPush.remove(uid);
                    }

                    target->syncItems.insert(eventId, qMakePair(eventId, etag));
                    target->pendingPush.remove(eventId);
                    saveSyncState(*target);
                });

        client->putEvent(refreshToken, remoteCalendarId, googleEventId, json);
        return;
    }

    if (entry.type != QLatin1String(kTypeCalDav)) {
        return;
    }

    QString username;
    QString password;
    if (!CredentialStore::readCredentials(entry.accountId, username, password)) {
        Q_EMIT syncError(entry.id, i18nc("@info", "No stored credentials for this account."));
        return;
    }

    ICalFormat format;
    const QByteArray ics = format.toICalString(event).toUtf8();

    QString etag;
    const auto it = entry.syncItems.constFind(event->uid());
    if (it != entry.syncItems.constEnd()) {
        etag = it.value().second;
    }

    auto *client = new CalDavClient(this);
    const QString calendarId = entry.id;
    const QString uid = event->uid();
    const QUrl remoteUrl(entry.remoteUrl);

    connect(client, &CalDavClient::eventPut, this, [this, client, calendarId, uid](const QString &href, const QString &newEtag, const QString &error) {
        client->deleteLater();

        if (!error.isEmpty()) {
            Q_EMIT syncError(calendarId, error);
            return;
        }

        LocalCalendar *target = findCalendarById(calendarId);
        if (!target) {
            return;
        }

        target->syncItems.insert(uid, qMakePair(href, newEtag));
        target->pendingPush.remove(uid);
        saveSyncState(*target);
    });

    client->putEvent(remoteUrl, uid, ics, etag, username, password);
}

void CalendarManager::pushDelete(LocalCalendar &entry, const QString &uid)
{
    if (entry.type == QLatin1String(kTypeGoogle)) {
        const auto syncIt = entry.syncItems.constFind(uid);
        if (syncIt == entry.syncItems.constEnd()) {
            return;
        }

        QString email;
        QString refreshToken;
        if (!CredentialStore::readGoogleTokens(entry.accountId, email, refreshToken)) {
            return;
        }

        const QString googleEventId = syncIt.value().first;
        const QString remoteCalendarId = entry.remoteUrl;
        entry.syncItems.remove(uid);
        entry.pendingPush.remove(uid);
        saveSyncState(entry);

        auto *client = new GoogleCalendarClient(this);
        connect(client, &GoogleCalendarClient::eventDeleted, client, &QObject::deleteLater);
        client->deleteEvent(refreshToken, remoteCalendarId, googleEventId);
        return;
    }

    if (entry.type != QLatin1String(kTypeCalDav)) {
        return;
    }

    const auto it = entry.syncItems.constFind(uid);
    if (it == entry.syncItems.constEnd()) {
        return;
    }

    QString username;
    QString password;
    if (!CredentialStore::readCredentials(entry.accountId, username, password)) {
        return;
    }

    const QString href = it.value().first;
    const QString etag = it.value().second;
    entry.syncItems.remove(uid);
    entry.pendingPush.remove(uid);
    saveSyncState(entry);

    auto *client = new CalDavClient(this);
    connect(client, &CalDavClient::eventDeleted, client, &QObject::deleteLater);
    client->deleteEvent(QUrl(href), etag, username, password);
}

QString CalendarManager::addCalendar(const QString &name, const QString &color)
{
    LocalCalendar entry;
    entry.id = CalFormat::createUniqueId();
    entry.name = name;
    entry.color = color;
    entry.visible = true;
    entry.calendar = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::systemTimeZone()));
    entry.storage = FileStorage::Ptr(new FileStorage(entry.calendar, icsPathFor(entry.id)));
    entry.storage->load();

    m_calendars.append(entry);
    saveCalendarsMeta();

    Q_EMIT calendarsChanged();
    Q_EMIT calendarChanged();
    return entry.id;
}

bool CalendarManager::removeCalendar(const QString &calendarId)
{
    if (m_calendars.size() <= 1) {
        return false;
    }

    for (int i = 0; i < m_calendars.size(); ++i) {
        if (m_calendars.at(i).id == calendarId) {
            const LocalCalendar removed = m_calendars.at(i);

            QFile::remove(icsPathFor(calendarId));
            if (removed.type == QLatin1String(kTypeCalDav) || removed.type == QLatin1String(kTypeGoogle)) {
                QFile::remove(syncStatePath(calendarId));
            }
            m_calendars.removeAt(i);
            saveCalendarsMeta();

            // If this was the last calendar for its CalDAV/Google account, drop the account too.
            if (removed.type == QLatin1String(kTypeCalDav) || removed.type == QLatin1String(kTypeGoogle)) {
                bool accountStillUsed = false;
                for (const LocalCalendar &other : m_calendars) {
                    if (other.accountId == removed.accountId) {
                        accountStillUsed = true;
                        break;
                    }
                }
                if (!accountStillUsed) {
                    CredentialStore::removeCredentials(removed.accountId);
                    for (int a = 0; a < m_accounts.size(); ++a) {
                        if (m_accounts.at(a).id == removed.accountId) {
                            m_accounts.removeAt(a);
                            break;
                        }
                    }
                    saveAccountsMeta();
                }
            }

            Q_EMIT calendarsChanged();
            Q_EMIT calendarChanged();
            return true;
        }
    }
    return false;
}

bool CalendarManager::updateCalendar(const QString &calendarId, const QString &name, const QString &color)
{
    LocalCalendar *entry = findCalendarById(calendarId);
    if (!entry) {
        return false;
    }

    entry->name = name;
    entry->color = color;
    saveCalendarsMeta();

    Q_EMIT calendarsChanged();
    Q_EMIT calendarChanged();
    return true;
}

bool CalendarManager::setCalendarVisible(const QString &calendarId, bool visible)
{
    LocalCalendar *entry = findCalendarById(calendarId);
    if (!entry) {
        return false;
    }

    entry->visible = visible;
    saveCalendarsMeta();

    Q_EMIT calendarsChanged();
    Q_EMIT calendarChanged();
    return true;
}

void CalendarManager::addCalDavAccount(const QString &serverUrl, const QString &username, const QString &password)
{
    QUrl url(serverUrl.trimmed());
    if (url.scheme().isEmpty()) {
        url.setScheme(QStringLiteral("https"));
    }

    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT calDavAccountAdded(QString(), 0, i18nc("@info", "Invalid server URL."));
        return;
    }

    auto *client = new CalDavClient(this);
    connect(client, &CalDavClient::discoveryFinished, this,
            [this, client, url, username, password](const QList<CalDavClient::CalendarInfo> &discovered, const QString &error) {
                client->deleteLater();

                if (!error.isEmpty()) {
                    Q_EMIT calDavAccountAdded(QString(), 0, error);
                    return;
                }
                if (discovered.isEmpty()) {
                    Q_EMIT calDavAccountAdded(QString(), 0, i18nc("@info", "No calendars were found on this server."));
                    return;
                }

                const QString accountId = CalFormat::createUniqueId();
                if (!CredentialStore::storeCredentials(accountId, username, password)) {
                    Q_EMIT calDavAccountAdded(QString(), 0, i18nc("@info", "Could not save credentials to KWallet."));
                    return;
                }

                Account account;
                account.id = accountId;
                account.serverUrl = url.toString();
                account.username = username;
                m_accounts.append(account);
                saveAccountsMeta();

                QStringList newCalendarIds;
                for (const CalDavClient::CalendarInfo &info : discovered) {
                    LocalCalendar entry;
                    entry.id = CalFormat::createUniqueId();
                    entry.name = info.displayName;
                    entry.color = info.color.size() >= 7 ? info.color.left(7) : QString::fromLatin1(kDefaultCalendarColor);
                    entry.visible = true;
                    entry.type = QString::fromLatin1(kTypeCalDav);
                    entry.accountId = accountId;
                    entry.remoteUrl = info.url;
                    entry.calendar = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::systemTimeZone()));
                    entry.storage = FileStorage::Ptr(new FileStorage(entry.calendar, icsPathFor(entry.id)));
                    entry.storage->load();

                    m_calendars.append(entry);
                    newCalendarIds.append(entry.id);
                }
                saveCalendarsMeta();

                Q_EMIT calendarsChanged();
                Q_EMIT calendarChanged();
                Q_EMIT calDavAccountAdded(accountId, newCalendarIds.size(), QString());

                for (const QString &id : newCalendarIds) {
                    syncCalendar(id);
                }
            });

    client->discoverCalendars(url, username, password);
}

void CalendarManager::addGoogleAccount()
{
    auto *client = new GoogleCalendarClient(this);
    connect(client, &GoogleCalendarClient::authenticated, this, [this, client](const QString &refreshToken, const QString &email, const QString &error) {
        if (!error.isEmpty()) {
            client->deleteLater();
            Q_EMIT googleAccountAdded(QString(), 0, error);
            return;
        }

        const QString accountId = CalFormat::createUniqueId();
        if (!CredentialStore::storeGoogleTokens(accountId, email, refreshToken)) {
            client->deleteLater();
            Q_EMIT googleAccountAdded(QString(), 0, i18nc("@info", "Could not save credentials to KWallet."));
            return;
        }

        Account account;
        account.id = accountId;
        account.type = QString::fromLatin1(kTypeGoogle);
        account.username = email;
        m_accounts.append(account);
        saveAccountsMeta();

        connect(client, &GoogleCalendarClient::calendarsListed, this,
                [this, client, accountId](const QList<GoogleCalendarClient::CalendarInfo> &discovered, const QString &error) {
                    client->deleteLater();

                    if (!error.isEmpty()) {
                        Q_EMIT googleAccountAdded(accountId, 0, error);
                        return;
                    }
                    if (discovered.isEmpty()) {
                        Q_EMIT googleAccountAdded(accountId, 0, i18nc("@info", "No calendars were found on this account."));
                        return;
                    }

                    QStringList newCalendarIds;
                    for (const GoogleCalendarClient::CalendarInfo &info : discovered) {
                        LocalCalendar entry;
                        entry.id = CalFormat::createUniqueId();
                        entry.name = info.displayName;
                        entry.color = info.color.size() >= 7 ? info.color.left(7) : QString::fromLatin1(kDefaultCalendarColor);
                        entry.visible = true;
                        entry.type = QString::fromLatin1(kTypeGoogle);
                        entry.accountId = accountId;
                        entry.remoteUrl = info.id;
                        entry.calendar = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::systemTimeZone()));
                        entry.storage = FileStorage::Ptr(new FileStorage(entry.calendar, icsPathFor(entry.id)));
                        entry.storage->load();

                        m_calendars.append(entry);
                        newCalendarIds.append(entry.id);
                    }
                    saveCalendarsMeta();

                    Q_EMIT calendarsChanged();
                    Q_EMIT calendarChanged();
                    Q_EMIT googleAccountAdded(accountId, newCalendarIds.size(), QString());

                    for (const QString &id : newCalendarIds) {
                        syncCalendar(id);
                    }
                });

        client->listCalendars(refreshToken);
    });

    client->authenticate();
}

void CalendarManager::syncCalendar(const QString &calendarId)
{
    LocalCalendar *entry = findCalendarById(calendarId);
    if (!entry) {
        return;
    }

    if (entry->type == QLatin1String(kTypeCalDav)) {
        syncCalDavCalendar(*entry);
    } else if (entry->type == QLatin1String(kTypeGoogle)) {
        syncGoogleCalendar(*entry);
    }
}

void CalendarManager::syncCalDavCalendar(LocalCalendar &entry)
{
    QString username;
    QString password;
    if (!CredentialStore::readCredentials(entry.accountId, username, password)) {
        Q_EMIT syncError(entry.id, i18nc("@info", "No stored credentials for this account."));
        return;
    }

    Q_EMIT syncStarted(entry.id);

    const QString calendarId = entry.id;
    const QUrl remoteUrl(entry.remoteUrl);

    auto *client = new CalDavClient(this);
    connect(client, &CalDavClient::eventsFetched, this, [this, client, calendarId](const QList<CalDavClient::RemoteEvent> &events, const QString &error) {
        client->deleteLater();

        LocalCalendar *target = findCalendarById(calendarId);
        if (!target) {
            return;
        }

        if (!error.isEmpty()) {
            Q_EMIT syncError(calendarId, error);
            return;
        }

        ICalFormat format;
        QSet<QString> remoteUids;

        for (const CalDavClient::RemoteEvent &remote : events) {
            MemoryCalendar::Ptr temp(new MemoryCalendar(QTimeZone::systemTimeZone()));
            if (!format.fromRawString(temp, remote.data.toUtf8())) {
                continue;
            }

            // A resource may contain the master VEVENT plus RECURRENCE-ID overrides
            // (exception instances) for the same uid.
            Event::Ptr master;
            Event::List exceptions;
            for (const Incidence::Ptr &incidence : temp->incidences()) {
                const Event::Ptr event = incidence.dynamicCast<Event>();
                if (!event) {
                    continue;
                }
                if (event->hasRecurrenceId()) {
                    exceptions.append(event);
                } else if (!master) {
                    master = event;
                }
            }

            if (!master) {
                continue;
            }

            const QString uid = master->uid();
            remoteUids.insert(uid);

            const auto syncIt = target->syncItems.constFind(uid);
            if (syncIt != target->syncItems.constEnd() && syncIt.value().second == remote.etag) {
                continue;
            }

            if (target->pendingPush.contains(uid)) {
                // Local edit not pushed yet: keep our local copy, retry the push below.
                continue;
            }

            const Event::Ptr existing = target->calendar->event(uid);
            if (existing) {
                target->calendar->deleteEventInstances(existing);
                target->calendar->deleteEvent(existing);
            }
            target->calendar->addEvent(Event::Ptr(master->clone()));
            for (const Event::Ptr &exception : exceptions) {
                target->calendar->addEvent(Event::Ptr(exception->clone()));
            }
            target->syncItems.insert(uid, qMakePair(remote.href, remote.etag));
        }

        // Events that disappeared from the server are removed locally too.
        const QStringList knownUids = target->syncItems.keys();
        for (const QString &uid : knownUids) {
            if (remoteUids.contains(uid)) {
                continue;
            }
            const Event::Ptr existing = target->calendar->event(uid);
            if (existing) {
                target->calendar->deleteEventInstances(existing);
                target->calendar->deleteEvent(existing);
            }
            target->syncItems.remove(uid);
        }

        target->storage->save();
        saveSyncState(*target);

        Q_EMIT calendarChanged();
        Q_EMIT syncFinished(calendarId);

        retryPendingPushes(*target);
    });

    client->fetchEvents(remoteUrl, username, password);
}

void CalendarManager::syncGoogleCalendar(LocalCalendar &entry)
{
    syncGoogleCalendar(entry, false);
}

void CalendarManager::syncGoogleCalendar(LocalCalendar &entry, bool forceFullResync)
{
    QString email;
    QString refreshToken;
    if (!CredentialStore::readGoogleTokens(entry.accountId, email, refreshToken)) {
        Q_EMIT syncError(entry.id, i18nc("@info", "No stored credentials for this account."));
        return;
    }

    Q_EMIT syncStarted(entry.id);

    const QString calendarId = entry.id;
    const QString remoteCalendarId = entry.remoteUrl;
    const QString syncToken = forceFullResync ? QString() : entry.syncToken;

    auto *client = new GoogleCalendarClient(this);
    connect(client, &GoogleCalendarClient::eventsFetched, this,
            [this, client, calendarId](const QList<GoogleCalendarClient::RemoteEvent> &events, const QString &nextSyncToken, bool syncTokenInvalid, const QString &error) {
                client->deleteLater();

                LocalCalendar *target = findCalendarById(calendarId);
                if (!target) {
                    return;
                }

                if (syncTokenInvalid) {
                    target->syncToken.clear();
                    target->syncItems.clear();
                    saveSyncState(*target);
                    syncGoogleCalendar(*target, true);
                    return;
                }

                if (!error.isEmpty()) {
                    Q_EMIT syncError(calendarId, error);
                    return;
                }

                // Pass 1: master/regular events (no recurringEventId).
                for (const GoogleCalendarClient::RemoteEvent &remote : events) {
                    if (remote.json.contains(QStringLiteral("recurringEventId"))) {
                        continue;
                    }

                    if (target->pendingPush.contains(remote.id)) {
                        // Local edit not pushed yet: keep our local copy, retry the push below.
                        continue;
                    }

                    if (remote.json.value(QStringLiteral("status")).toString() == QLatin1String("cancelled")) {
                        const Event::Ptr existing = target->calendar->event(remote.id);
                        if (existing) {
                            target->calendar->deleteEventInstances(existing);
                            target->calendar->deleteEvent(existing);
                        }
                        target->syncItems.remove(remote.id);
                        continue;
                    }

                    const Event::Ptr event = googleJsonToEvent(remote.json);
                    const Event::Ptr existing = target->calendar->event(event->uid());
                    if (existing) {
                        target->calendar->deleteEvent(existing);
                    }
                    target->calendar->addEvent(event);
                    target->syncItems.insert(event->uid(), qMakePair(event->uid(), remote.etag));
                }

                // Pass 2: recurrence exception instances (override or cancellation of a
                // single occurrence of a recurring event).
                for (const GoogleCalendarClient::RemoteEvent &remote : events) {
                    if (!remote.json.contains(QStringLiteral("recurringEventId"))) {
                        continue;
                    }

                    const QString masterUid = remote.json.value(QStringLiteral("recurringEventId")).toString();
                    const QDateTime recurrenceId = googleOriginalStartTime(remote.json);
                    const QString key = masterUid + QLatin1Char('#') + recurrenceId.toString(Qt::ISODate);

                    if (target->pendingPush.contains(key)) {
                        continue;
                    }

                    const Event::Ptr existingException = target->calendar->event(masterUid, recurrenceId);

                    if (remote.json.value(QStringLiteral("status")).toString() == QLatin1String("cancelled")) {
                        // The occurrence was removed from the series: exclude it via EXDATE.
                        const Event::Ptr master = target->calendar->event(masterUid);
                        if (master) {
                            master->recurrence()->addExDateTime(recurrenceId);
                        }
                        if (existingException) {
                            target->calendar->deleteEvent(existingException);
                        }
                        target->syncItems.remove(key);
                        continue;
                    }

                    if (existingException) {
                        target->calendar->deleteEvent(existingException);
                    }
                    target->calendar->addEvent(googleJsonToExceptionEvent(remote.json));
                    target->syncItems.insert(key, qMakePair(remote.id, remote.etag));
                }

                target->syncToken = nextSyncToken;
                target->storage->save();
                saveSyncState(*target);

                Q_EMIT calendarChanged();
                Q_EMIT syncFinished(calendarId);

                retryPendingPushes(*target);
            });

    client->fetchEvents(refreshToken, remoteCalendarId, syncToken);
}

void CalendarManager::syncAll()
{
    for (const LocalCalendar &entry : m_calendars) {
        if (entry.type == QLatin1String(kTypeCalDav) || entry.type == QLatin1String(kTypeGoogle)) {
            syncCalendar(entry.id);
        }
    }
}

QString CalendarManager::addEvent(const QString &calendarId,
                                   const QString &summary,
                                   const QString &description,
                                   const QString &location,
                                   const QDateTime &start,
                                   const QDateTime &end,
                                   bool allDay,
                                   const QString &color,
                                   const QString &recurrence,
                                   int reminderMinutes,
                                   bool busy)
{
    LocalCalendar *entry = findCalendarById(calendarId);
    if (!entry && !m_calendars.isEmpty()) {
        entry = &m_calendars[0];
    }
    if (!entry) {
        return QString();
    }

    Event::Ptr event(new Event);
    event->setUid(CalFormat::createUniqueId());

    applyEventFields(event, summary, description, location, start, end, allDay, color, recurrence, reminderMinutes, busy);

    entry->calendar->addEvent(event);
    entry->storage->save();
    Q_EMIT calendarChanged();

    markPendingPush(*entry, event->uid());
    pushEvent(*entry, event);

    return event->uid();
}

bool CalendarManager::updateEvent(const QString &uid,
                                   const QString &summary,
                                   const QString &description,
                                   const QString &location,
                                   const QDateTime &start,
                                   const QDateTime &end,
                                   bool allDay,
                                   const QString &color,
                                   const QString &recurrence,
                                   int reminderMinutes,
                                   bool busy)
{
    Event::Ptr event;
    LocalCalendar *entry = findCalendarForEvent(uid, &event);
    if (!entry) {
        return false;
    }

    applyEventFields(event, summary, description, location, start, end, allDay, color, recurrence, reminderMinutes, busy);

    entry->storage->save();
    Q_EMIT calendarChanged();

    markPendingPush(*entry, event->uid());
    pushEvent(*entry, event);

    return true;
}

bool CalendarManager::removeEvent(const QString &uid)
{
    Event::Ptr event;
    LocalCalendar *entry = findCalendarForEvent(uid, &event);
    if (!entry) {
        return false;
    }

    entry->calendar->deleteEvent(event);
    entry->storage->save();
    Q_EMIT calendarChanged();

    pushDelete(*entry, uid);

    return true;
}

QVariantMap CalendarManager::eventData(const QString &uid) const
{
    QVariantMap map;

    for (const LocalCalendar &entry : m_calendars) {
        const Event::Ptr event = entry.calendar->event(uid);
        if (!event) {
            continue;
        }

        map.insert(QStringLiteral("uid"), event->uid());
        map.insert(QStringLiteral("calendarId"), entry.id);
        map.insert(QStringLiteral("summary"), event->summary());
        map.insert(QStringLiteral("description"), event->description());
        map.insert(QStringLiteral("location"), event->location());
        map.insert(QStringLiteral("start"), event->dtStart());
        map.insert(QStringLiteral("end"), event->dtEnd());
        map.insert(QStringLiteral("allDay"), event->allDay());
        map.insert(QStringLiteral("color"), event->customProperty(kPropertyApp, kColorProperty));
        map.insert(QStringLiteral("busy"), event->transparency() == Event::Opaque);

        QString recurrence = QStringLiteral("none");
        if (event->recurs()) {
            switch (event->recurrence()->recurrenceType()) {
            case RecurrenceRule::rDaily:
                recurrence = QStringLiteral("daily");
                break;
            case RecurrenceRule::rWeekly:
                recurrence = QStringLiteral("weekly");
                break;
            case RecurrenceRule::rMonthly:
                recurrence = QStringLiteral("monthly");
                break;
            case RecurrenceRule::rYearly:
                recurrence = QStringLiteral("yearly");
                break;
            default:
                break;
            }
        }
        map.insert(QStringLiteral("recurrence"), recurrence);

        int reminderMinutes = -1;
        const Alarm::List alarms = event->alarms();
        if (!alarms.isEmpty()) {
            reminderMinutes = int(-alarms.first()->startOffset().asSeconds() / 60);
        }
        map.insert(QStringLiteral("reminderMinutes"), reminderMinutes);

        break;
    }

    return map;
}

bool CalendarManager::rescheduleEvent(const QString &uid, qint64 secondsDelta)
{
    Event::Ptr event;
    LocalCalendar *entry = findCalendarForEvent(uid, &event);
    if (!entry) {
        return false;
    }

    event->setDtStart(event->dtStart().addSecs(secondsDelta));
    event->setDtEnd(event->dtEnd().addSecs(secondsDelta));

    entry->storage->save();
    Q_EMIT calendarChanged();

    markPendingPush(*entry, event->uid());
    pushEvent(*entry, event);

    return true;
}

bool CalendarManager::moveEventToCalendar(const QString &uid, const QString &calendarId)
{
    Event::Ptr event;
    LocalCalendar *source = findCalendarForEvent(uid, &event);
    if (!source) {
        return false;
    }

    if (source->id == calendarId) {
        return true;
    }

    LocalCalendar *target = findCalendarById(calendarId);
    if (!target) {
        return false;
    }

    pushDelete(*source, uid);

    const Event::Ptr clone(event->clone());
    source->calendar->deleteEvent(event);
    target->calendar->addEvent(clone);

    source->storage->save();
    target->storage->save();
    Q_EMIT calendarChanged();

    markPendingPush(*target, clone->uid());
    pushEvent(*target, clone);

    return true;
}
