#pragma once

#include <QObject>
#include <QDateTime>
#include <QQmlEngine>
#include <QVariantList>

#include <KCalendarCore/FileStorage>
#include <KCalendarCore/MemoryCalendar>

/**
 * Owns the local calendars (each an in-memory KCalendarCore::Calendar backed
 * by its own .ics file) and exposes CRUD operations on events and calendars
 * to QML.
 *
 * This is the Phase 1 "local calendar" backend. Remote backends (CalDAV,
 * Google, Microsoft Graph) will plug into additional calendars in later
 * phases.
 */
class CalendarManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// List of {id, name, color, visible} maps, one per local calendar.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)

public:
    /// A local calendar: metadata plus its backing in-memory calendar/storage.
    struct LocalCalendar {
        QString id;
        QString name;
        QString color;
        bool visible = true;
        KCalendarCore::MemoryCalendar::Ptr calendar;
        KCalendarCore::FileStorage::Ptr storage;
    };

    explicit CalendarManager(QObject *parent = nullptr);

    /// Internal C++ access for EventListModel: the list of local calendars.
    const QList<LocalCalendar> &localCalendars() const;

    QVariantList calendars() const;

    /// Creates a new empty local calendar. Returns its id.
    Q_INVOKABLE QString addCalendar(const QString &name, const QString &color);

    /// Removes a local calendar and its events. Refuses to remove the last one.
    Q_INVOKABLE bool removeCalendar(const QString &calendarId);

    /// Renames/recolors an existing local calendar.
    Q_INVOKABLE bool updateCalendar(const QString &calendarId, const QString &name, const QString &color);

    /// Shows or hides a calendar's events in the views.
    Q_INVOKABLE bool setCalendarVisible(const QString &calendarId, bool visible);

    /**
     * Creates a new event in the given calendar and stores it.
     *
     * @param recurrence one of "none", "daily", "weekly", "monthly", "yearly"
     * @param reminderMinutes minutes before the start to trigger a display
     *        alarm, or -1 for no reminder
     * @param busy whether the event should count as "busy" (opaque) time
     * @return the UID of the newly created event
     */
    Q_INVOKABLE QString addEvent(const QString &calendarId,
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
     * Returns the fields of the event with the given UID as a map, suitable
     * for pre-filling an edit dialog. Returns an empty map if not found.
     */
    Q_INVOKABLE QVariantMap eventData(const QString &uid) const;

    /**
     * Shifts both the start and end of an event by @p secondsDelta,
     * preserving its duration. Used for "postpone" (positive delta) and
     * "advance" (negative delta) quick actions. Returns false if not found.
     */
    Q_INVOKABLE bool rescheduleEvent(const QString &uid, qint64 secondsDelta);

    /// Moves an event to a different local calendar. Returns false if either is not found.
    Q_INVOKABLE bool moveEventToCalendar(const QString &uid, const QString &calendarId);

Q_SIGNALS:
    void calendarChanged();
    void calendarsChanged();

private:
    void loadCalendars();
    void saveCalendarsMeta();
    QString calendarsMetaPath() const;
    QString icsPathFor(const QString &id) const;

    LocalCalendar *findCalendarById(const QString &id);
    const LocalCalendar *findCalendarById(const QString &id) const;
    LocalCalendar *findCalendarForEvent(const QString &uid, KCalendarCore::Event::Ptr *eventOut = nullptr);

    QList<LocalCalendar> m_calendars;
};
