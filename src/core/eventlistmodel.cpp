#include "eventlistmodel.h"
#include "calendarmanager.h"

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Event>
#include <KCalendarCore/OccurrenceIterator>
#include <KCalendarCore/Recurrence>

using namespace KCalendarCore;

namespace
{
constexpr auto kPropertyApp = "KWEEK";
constexpr auto kColorProperty = "COLOR";
}

EventListModel::EventListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

CalendarManager *EventListModel::calendarManager() const
{
    return m_calendarManager;
}

void EventListModel::setCalendarManager(CalendarManager *manager)
{
    if (m_calendarManager == manager) {
        return;
    }

    if (m_calendarManager) {
        disconnect(m_calendarManager, nullptr, this, nullptr);
    }

    m_calendarManager = manager;

    if (m_calendarManager) {
        connect(m_calendarManager, &CalendarManager::calendarChanged, this, &EventListModel::refresh);
    }

    Q_EMIT calendarManagerChanged();
    refresh();
}

QDateTime EventListModel::rangeStart() const
{
    return m_rangeStart;
}

void EventListModel::setRangeStart(const QDateTime &start)
{
    if (m_rangeStart == start) {
        return;
    }

    m_rangeStart = start;
    Q_EMIT rangeChanged();
    refresh();
}

QDateTime EventListModel::rangeEnd() const
{
    return m_rangeEnd;
}

void EventListModel::setRangeEnd(const QDateTime &end)
{
    if (m_rangeEnd == end) {
        return;
    }

    m_rangeEnd = end;
    Q_EMIT rangeChanged();
    refresh();
}

int EventListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_occurrences.size();
}

QVariant EventListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_occurrences.size()) {
        return {};
    }

    const Occurrence &occurrence = m_occurrences.at(index.row());
    const Incidence::Ptr &incidence = occurrence.incidence;

    switch (role) {
    case UidRole:
        return occurrence.uid;
    case SummaryRole:
        return incidence->summary();
    case DescriptionRole:
        return incidence->description();
    case LocationRole:
        return incidence->location();
    case StartRole:
        return occurrence.start;
    case EndRole:
        return occurrence.end;
    case AllDayRole:
        return incidence->allDay();
    case ColorRole:
        return incidence->customProperty(kPropertyApp, kColorProperty);
    case RecurringRole:
        return incidence->recurs();
    case RecurrenceRole: {
        if (!incidence->recurs()) {
            return QStringLiteral("none");
        }
        switch (incidence->recurrence()->recurrenceType()) {
        case RecurrenceRule::rDaily:
            return QStringLiteral("daily");
        case RecurrenceRule::rWeekly:
            return QStringLiteral("weekly");
        case RecurrenceRule::rMonthly:
            return QStringLiteral("monthly");
        case RecurrenceRule::rYearly:
            return QStringLiteral("yearly");
        default:
            return QStringLiteral("none");
        }
    }
    case ReminderMinutesRole: {
        const Alarm::List alarms = incidence->alarms();
        if (alarms.isEmpty()) {
            return -1;
        }
        return int(-alarms.first()->startOffset().asSeconds() / 60);
    }
    case BusyRole: {
        const Event::Ptr event = incidence.dynamicCast<Event>();
        return event && event->transparency() == Event::Opaque;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> EventListModel::roleNames() const
{
    return {
        {UidRole, "uid"},
        {SummaryRole, "summary"},
        {DescriptionRole, "description"},
        {LocationRole, "location"},
        {StartRole, "start"},
        {EndRole, "end"},
        {AllDayRole, "allDay"},
        {ColorRole, "color"},
        {RecurringRole, "recurring"},
        {RecurrenceRole, "recurrence"},
        {ReminderMinutesRole, "reminderMinutes"},
        {BusyRole, "busy"},
    };
}

void EventListModel::refresh()
{
    beginResetModel();
    m_occurrences.clear();

    if (m_calendarManager && m_rangeStart.isValid() && m_rangeEnd.isValid()) {
        OccurrenceIterator it(*m_calendarManager->calendar(), m_rangeStart, m_rangeEnd);
        while (it.hasNext()) {
            it.next();
            m_occurrences.append({it.incidence()->uid(), it.occurrenceStartDate(), it.occurrenceEndDate(), it.incidence()});
        }

        std::sort(m_occurrences.begin(), m_occurrences.end(), [](const Occurrence &a, const Occurrence &b) {
            return a.start < b.start;
        });
    }

    endResetModel();
}
