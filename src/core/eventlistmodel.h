#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>

#include <KCalendarCore/Event>

class CalendarManager;

/**
 * Lists event occurrences (expanding recurrences) that fall within
 * [rangeStart, rangeEnd), sorted by start time. Backed by CalendarManager's
 * calendar.
 */
class EventListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(CalendarManager *calendarManager READ calendarManager WRITE setCalendarManager NOTIFY calendarManagerChanged)
    Q_PROPERTY(QDateTime rangeStart READ rangeStart WRITE setRangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDateTime rangeEnd READ rangeEnd WRITE setRangeEnd NOTIFY rangeChanged)

public:
    enum Roles {
        UidRole = Qt::UserRole + 1,
        SummaryRole,
        DescriptionRole,
        LocationRole,
        StartRole,
        EndRole,
        AllDayRole,
        ColorRole,
        RecurringRole,
        RecurrenceRole,
        ReminderMinutesRole,
        BusyRole,
    };

    explicit EventListModel(QObject *parent = nullptr);

    CalendarManager *calendarManager() const;
    void setCalendarManager(CalendarManager *manager);

    QDateTime rangeStart() const;
    void setRangeStart(const QDateTime &start);

    QDateTime rangeEnd() const;
    void setRangeEnd(const QDateTime &end);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Re-reads occurrences from the calendar for the current range.
    Q_INVOKABLE void refresh();

    /// Returns all role values for the occurrence at @p row as a map, for convenient iteration from QML.
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void calendarManagerChanged();
    void rangeChanged();

private:
    struct Occurrence {
        QString uid;
        QDateTime start;
        QDateTime end;
        KCalendarCore::Incidence::Ptr incidence;
    };

    CalendarManager *m_calendarManager = nullptr;
    QDateTime m_rangeStart;
    QDateTime m_rangeEnd;
    QList<Occurrence> m_occurrences;
};
