#include "calendarmanager.h"

#include <QDir>
#include <QStandardPaths>
#include <QTimeZone>

#include <KCalendarCore/Alarm>
#include <KCalendarCore/CalFormat>
#include <KCalendarCore/Event>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>

using namespace KCalendarCore;

namespace
{
constexpr auto kPropertyApp = "KWEEK";
constexpr auto kColorProperty = "COLOR";

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
    , m_calendar(new MemoryCalendar(QTimeZone::systemTimeZone()))
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    m_storage = FileStorage::Ptr(new FileStorage(m_calendar, dataDir + QStringLiteral("/calendar.ics")));
    load();
}

MemoryCalendar::Ptr CalendarManager::calendar() const
{
    return m_calendar;
}

QString CalendarManager::addEvent(const QString &summary,
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
    Event::Ptr event(new Event);
    event->setUid(CalFormat::createUniqueId());

    applyEventFields(event, summary, description, location, start, end, allDay, color, recurrence, reminderMinutes, busy);

    m_calendar->addEvent(event);
    save();

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
    const Event::Ptr event = m_calendar->event(uid);
    if (!event) {
        return false;
    }

    applyEventFields(event, summary, description, location, start, end, allDay, color, recurrence, reminderMinutes, busy);

    save();
    return true;
}

bool CalendarManager::removeEvent(const QString &uid)
{
    const Event::Ptr event = m_calendar->event(uid);
    if (!event) {
        return false;
    }

    m_calendar->deleteEvent(event);
    save();
    return true;
}

QVariantMap CalendarManager::eventData(const QString &uid) const
{
    QVariantMap map;

    const Event::Ptr event = m_calendar->event(uid);
    if (!event) {
        return map;
    }

    map.insert(QStringLiteral("uid"), event->uid());
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

    return map;
}

bool CalendarManager::rescheduleEvent(const QString &uid, qint64 secondsDelta)
{
    const Event::Ptr event = m_calendar->event(uid);
    if (!event) {
        return false;
    }

    event->setDtStart(event->dtStart().addSecs(secondsDelta));
    event->setDtEnd(event->dtEnd().addSecs(secondsDelta));

    save();
    return true;
}

void CalendarManager::load()
{
    m_storage->load();
    Q_EMIT calendarChanged();
}

void CalendarManager::save()
{
    m_storage->save();
    Q_EMIT calendarChanged();
}
