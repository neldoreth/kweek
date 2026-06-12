#pragma once

#include <QObject>
#include <QDateTime>
#include <QMap>
#include <QPair>
#include <QQmlEngine>
#include <QVariantList>

#include <KCalendarCore/FileStorage>
#include <KCalendarCore/MemoryCalendar>

/**
 * Owns the local calendars (each an in-memory KCalendarCore::Calendar backed
 * by its own .ics file) and exposes CRUD operations on events and calendars
 * to QML.
 *
 * Also owns CalDAV-backed calendars (Phase 2): these behave like local
 * calendars but are additionally synced with a remote server. Local edits
 * are pushed immediately; syncCalendar() pulls remote changes.
 */
class CalendarManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// List of {id, name, color, visible, type, accountId} maps, one per calendar.
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)

public:
    /// Maps an event UID to its remote {href, etag} for CalDAV calendars.
    using SyncItems = QMap<QString, QPair<QString, QString>>;

    /// A calendar: metadata plus its backing in-memory calendar/storage.
    struct LocalCalendar {
        QString id;
        QString name;
        QString color;
        bool visible = true;
        /// "local" or "caldav".
        QString type = QStringLiteral("local");
        /// Only set for type == "caldav": id of the owning account.
        QString accountId;
        /// Only set for type == "caldav": URL of the remote calendar collection.
        QString remoteUrl;
        KCalendarCore::MemoryCalendar::Ptr calendar;
        KCalendarCore::FileStorage::Ptr storage;
        /// Only used for type == "caldav": uid -> {href, etag}.
        SyncItems syncItems;
    };

    /// A CalDAV account: credentials are stored separately in KWallet.
    struct Account {
        QString id;
        QString serverUrl;
        QString username;
    };

    explicit CalendarManager(QObject *parent = nullptr);

    /// Internal C++ access for EventListModel: the list of local calendars.
    const QList<LocalCalendar> &localCalendars() const;

    QVariantList calendars() const;

    /// Creates a new empty local calendar. Returns its id.
    Q_INVOKABLE QString addCalendar(const QString &name, const QString &color);

    /// Removes a calendar and its events. Refuses to remove the last one.
    Q_INVOKABLE bool removeCalendar(const QString &calendarId);

    /// Renames/recolors an existing calendar.
    Q_INVOKABLE bool updateCalendar(const QString &calendarId, const QString &name, const QString &color);

    /// Shows or hides a calendar's events in the views.
    Q_INVOKABLE bool setCalendarVisible(const QString &calendarId, bool visible);

    /**
     * Connects a CalDAV account: discovers the calendar collections on
     * @p serverUrl with the given credentials, stores the credentials in
     * KWallet and adds one local calendar per discovered remote calendar.
     * Emits calDavAccountAdded() with the result.
     */
    Q_INVOKABLE void addCalDavAccount(const QString &serverUrl, const QString &username, const QString &password);

    /// Pulls remote changes for a CalDAV calendar. No-op for local calendars.
    Q_INVOKABLE void syncCalendar(const QString &calendarId);

    /// Syncs all CalDAV calendars.
    Q_INVOKABLE void syncAll();

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

    /// Moves an event to a different calendar. Returns false if either is not found.
    Q_INVOKABLE bool moveEventToCalendar(const QString &uid, const QString &calendarId);

Q_SIGNALS:
    void calendarChanged();
    void calendarsChanged();

    /// Result of addCalDavAccount(): @p error is empty on success.
    void calDavAccountAdded(const QString &accountId, int calendarCount, const QString &error);

    void syncStarted(const QString &calendarId);
    void syncFinished(const QString &calendarId);
    void syncError(const QString &calendarId, const QString &error);

private:
    void loadCalendars();
    void saveCalendarsMeta();
    QString calendarsMetaPath() const;
    QString icsPathFor(const QString &id) const;
    QString syncStatePath(const QString &id) const;
    void loadSyncState(LocalCalendar &entry);
    void saveSyncState(const LocalCalendar &entry);

    void loadAccounts();
    void saveAccountsMeta();
    QString accountsMetaPath() const;

    LocalCalendar *findCalendarById(const QString &id);
    const LocalCalendar *findCalendarById(const QString &id) const;
    LocalCalendar *findCalendarForEvent(const QString &uid, KCalendarCore::Event::Ptr *eventOut = nullptr);

    /// Pushes a create/update of @p event to its calendar's CalDAV server, if any.
    void pushEvent(LocalCalendar &entry, const KCalendarCore::Event::Ptr &event);

    /// Pushes a deletion of @p uid to its calendar's CalDAV server, if any.
    void pushDelete(LocalCalendar &entry, const QString &uid);

    QList<LocalCalendar> m_calendars;
    QList<Account> m_accounts;
};
