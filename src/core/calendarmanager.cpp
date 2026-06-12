#include "calendarmanager.h"

#include "caldavclient.h"
#include "credentialstore.h"

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

        if (entry.type == QLatin1String(kTypeCalDav)) {
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
        if (entry.type == QLatin1String(kTypeCalDav)) {
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

void CalendarManager::pushEvent(LocalCalendar &entry, const Event::Ptr &event)
{
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
        saveSyncState(*target);
    });

    client->putEvent(remoteUrl, uid, ics, etag, username, password);
}

void CalendarManager::pushDelete(LocalCalendar &entry, const QString &uid)
{
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
            if (removed.type == QLatin1String(kTypeCalDav)) {
                QFile::remove(syncStatePath(calendarId));
            }
            m_calendars.removeAt(i);
            saveCalendarsMeta();

            // If this was the last calendar for its CalDAV account, drop the account too.
            if (removed.type == QLatin1String(kTypeCalDav)) {
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

void CalendarManager::syncCalendar(const QString &calendarId)
{
    LocalCalendar *entry = findCalendarById(calendarId);
    if (!entry || entry->type != QLatin1String(kTypeCalDav)) {
        return;
    }

    QString username;
    QString password;
    if (!CredentialStore::readCredentials(entry->accountId, username, password)) {
        Q_EMIT syncError(calendarId, i18nc("@info", "No stored credentials for this account."));
        return;
    }

    Q_EMIT syncStarted(calendarId);

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

            for (const Incidence::Ptr &incidence : temp->incidences()) {
                // Recurring-event exceptions are not yet handled by sync (v1 limitation).
                if (incidence->hasRecurrenceId()) {
                    continue;
                }

                const Event::Ptr event = incidence.dynamicCast<Event>();
                if (!event) {
                    continue;
                }

                const QString uid = event->uid();
                remoteUids.insert(uid);

                const auto syncIt = target->syncItems.constFind(uid);
                if (syncIt != target->syncItems.constEnd() && syncIt.value().second == remote.etag) {
                    continue;
                }

                const Event::Ptr existing = target->calendar->event(uid);
                if (existing) {
                    target->calendar->deleteEvent(existing);
                }
                target->calendar->addEvent(Event::Ptr(event->clone()));
                target->syncItems.insert(uid, qMakePair(remote.href, remote.etag));
            }
        }

        // Events that disappeared from the server are removed locally too.
        const QStringList knownUids = target->syncItems.keys();
        for (const QString &uid : knownUids) {
            if (remoteUids.contains(uid)) {
                continue;
            }
            const Event::Ptr existing = target->calendar->event(uid);
            if (existing) {
                target->calendar->deleteEvent(existing);
            }
            target->syncItems.remove(uid);
        }

        target->storage->save();
        saveSyncState(*target);

        Q_EMIT calendarChanged();
        Q_EMIT syncFinished(calendarId);
    });

    client->fetchEvents(QUrl(entry->remoteUrl), username, password);
}

void CalendarManager::syncAll()
{
    for (const LocalCalendar &entry : m_calendars) {
        if (entry.type == QLatin1String(kTypeCalDav)) {
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

    pushEvent(*target, clone);

    return true;
}
