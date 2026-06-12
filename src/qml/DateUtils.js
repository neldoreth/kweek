.pragma library

// Calendar week starts on Monday.

function startOfDay(date) {
    const d = new Date(date);
    d.setHours(0, 0, 0, 0);
    return d;
}

function addDays(date, days) {
    const d = new Date(date);
    d.setDate(d.getDate() + days);
    return d;
}

function startOfWeek(date) {
    const d = startOfDay(date);
    // getDay(): 0 = Sunday .. 6 = Saturday. Shift so Monday = 0.
    const offset = (d.getDay() + 6) % 7;
    return addDays(d, -offset);
}

function startOfMonth(date) {
    const d = startOfDay(date);
    d.setDate(1);
    return d;
}

function addMonths(date, months) {
    const d = new Date(date);
    d.setMonth(d.getMonth() + months);
    return d;
}

// First cell shown in a month grid: the Monday of the week containing the 1st.
function monthGridStart(date) {
    return startOfWeek(startOfMonth(date));
}

function isSameDay(a, b) {
    return a.getFullYear() === b.getFullYear()
        && a.getMonth() === b.getMonth()
        && a.getDate() === b.getDate();
}
