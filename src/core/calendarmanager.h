#pragma once

#include <QObject>
#include <QDateTime>
#include <QQmlEngine>

#include <KCalendarCore/FileStorage>
#include <KCalendarCore/MemoryCalendar>

/**
 * Owns the local calendar (an in-memory KCalendarCore::Calendar backed by a
 * single .ics file) and exposes CRUD operations on events to QML.
 *
 * This is the Phase 1 "local calendar" backend. Remote backends (CalDAV,
 * Google, Microsoft Graph) will plug into the same Calendar in later phases.
 */
class CalendarManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit CalendarManager(QObject *parent = nullptr);

    KCalendarCore::MemoryCalendar::Ptr calendar() const;

    /**
     * Creates a new event and stores it.
     *
     * @param recurrence one of "none", "daily", "weekly", "monthly", "yearly"
     * @param reminderMinutes minutes before the start to trigger a display
     *        alarm, or -1 for no reminder
     * @param busy whether the event should count as "busy" (opaque) time
     * @return the UID of the newly created event
     */
    Q_INVOKABLE QString addEvent(const QString &summary,
                                  const QString &description,
                                  const QString &location,
                                  const QDateTime &start,
                                  const QDateTime &end,
                                  bool allDay,
                                  const QString &color,
                                  const QString &recurrence,
                                  int reminderMinutes,
                                  bool busy);

    /**
     * Updates all fields of an existing event. Returns false if no event
     * with the given UID exists.
     */
    Q_INVOKABLE bool updateEvent(const QString &uid,
                                  const QString &summary,
                                  const QString &description,
                                  const QString &location,
                                  const QDateTime &start,
                                  const QDateTime &end,
                                  bool allDay,
                                  const QString &color,
                                  const QString &recurrence,
                                  int reminderMinutes,
                                  bool busy);

    /// Removes the event with the given UID. Returns false if not found.
    Q_INVOKABLE bool removeEvent(const QString &uid);

    /**
     * Shifts both the start and end of an event by @p secondsDelta,
     * preserving its duration. Used for "postpone" (positive delta) and
     * "advance" (negative delta) quick actions. Returns false if not found.
     */
    Q_INVOKABLE bool rescheduleEvent(const QString &uid, qint64 secondsDelta);

Q_SIGNALS:
    void calendarChanged();

private:
    void load();
    void save();

    KCalendarCore::MemoryCalendar::Ptr m_calendar;
    KCalendarCore::FileStorage::Ptr m_storage;
};
