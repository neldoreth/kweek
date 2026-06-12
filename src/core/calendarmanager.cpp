#include "calendarmanager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimeZone>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/CalFormat>
#include <KCalendarCore/Event>
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
    loadCalendars();
}

QString CalendarManager::calendarsMetaPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/calendars.json");
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
        entry.calendar = MemoryCalendar::Ptr(new MemoryCalendar(QTimeZone::systemTimeZone()));
        entry.storage = FileStorage::Ptr(new FileStorage(entry.calendar, icsPathFor(entry.id)));
        entry.storage->load();

        m_calendars.append(entry);
    }

    saveCalendarsMeta();
}

void CalendarManager::saveCalendarsMeta()
{
    QJsonArray array;
    for (const LocalCalendar &entry : m_calendars) {
        array.append(QJsonObject{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("color"), entry.color},
            {QStringLiteral("visible"), entry.visible},
        });
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
            QFile::remove(icsPathFor(calendarId));
            m_calendars.removeAt(i);
            saveCalendarsMeta();

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

    const Event::Ptr clone(event->clone());
    source->calendar->deleteEvent(event);
    target->calendar->addEvent(clone);

    source->storage->save();
    target->storage->save();
    Q_EMIT calendarChanged();
    return true;
}
