# Kweek — Cliente de calendario moderno para KDE

> Nombre de la app: **Kweek**. App ID provisional: `io.github.neldoreth.Kweek`
> (a confirmar cuando se cree el repositorio en GitHub).
> Icono inicial en `icons/io.github.neldoreth.Kweek.svg` (página de calendario
> con fila de puntos de "semana" destacando el día actual).

## Visión del proyecto

Aplicación de calendario para escritorio, con integración nativa en KDE Plasma,
distribuible como **Flatpak** y lo más agnóstica posible respecto a la
distribución Linux. La inspiración visual y funcional son **BusyCal** y
**Fantastical** (macOS): interfaz moderna, fluida, con vistas potentes y
manejo directo (drag & drop) de eventos.

No es objetivo de este proyecto el lenguaje natural para crear eventos
(a diferencia de Fantastical). Sí es objetivo soportar múltiples cuentas
y proveedores de calendario con métodos de login modernos (OAuth).

## Estado actual

Esqueleto inicial creado y compilando: app Qt6/Kirigami mínima con CMake +
ECM/KDE Frameworks 6.

Modelo de datos de eventos implementado sobre KCalendarCore:

- `src/core/calendarmanager.h/.cpp` — `CalendarManager` (singleton QML):
  gestiona una lista de **calendarios** (`LocalCalendar`: id, nombre, color,
  visible, `type` "local"/"caldav"), cada uno con su propio
  `KCalendarCore::MemoryCalendar` + `FileStorage`. Metadatos en
  `~/.local/share/Kweek/Kweek/calendars.json`; `.ics` por calendario en
  `~/.local/share/Kweek/Kweek/calendar.ics` (calendario "default"/"Personal",
  creado automáticamente la primera vez) y `.../calendars/<id>.ics` para el
  resto. Propiedad `calendars` (lista de mapas id/name/color/visible/type/
  accountId) y métodos `addCalendar`, `removeCalendar` (no permite borrar el
  último; si es CalDAV y era el último calendario de su cuenta, borra también
  la cuenta y sus credenciales), `updateCalendar` (nombre+color),
  `setCalendarVisible`. CRUD de eventos: `addEvent(calendarId, ...)`,
  `updateEvent`, `removeEvent`, `eventData` (incluye `calendarId`),
  `rescheduleEvent` (posponer/adelantar manteniendo duración) y
  `moveEventToCalendar(uid, calendarId)`; todas localizan el calendario
  contenedor buscando el UID. Cada evento soporta: resumen, descripción,
  ubicación, inicio/fin, todo el día, color opcional por evento (custom
  property `X-KDE-KWEEK-COLOR`, si está vacío se usa el color del
  calendario), recurrencia simple (ninguna/diaria/semanal/mensual/anual),
  recordatorio (alarma Display N minutos antes) y disponibilidad
  ocupado/libre (TRANSP).
- **CalDAV genérico (iCloud, Nextcloud, Fastmail)**: `CalendarManager` puede
  además gestionar **calendarios CalDAV** (`type: "caldav"`), persistidos
  igual que los locales pero con `accountId` + `remoteUrl` adicionales y un
  fichero de estado de sync `.../calendars/<id>.sync.json` (uid -> {href,
  etag}). Cuentas en `~/.local/share/Kweek/Kweek/accounts.json` (id,
  serverUrl, username — la contraseña/app-password se guarda en KWallet,
  carpeta "Kweek", vía `src/core/credentialstore.h/.cpp`).
  - `src/core/caldavclient.h/.cpp` — `CalDavClient`: cliente CalDAV genérico
    sobre `QNetworkAccessManager` (auth Basic). `discoverCalendars()`
    encadena PROPFIND `current-user-principal` -> PROPFIND
    `calendar-home-set` -> PROPFIND Depth:1 de la colección home (filtra
    `resourcetype` que no sea `calendar` o sea schedule-inbox/outbox),
    extrayendo `displayname` y `calendar-color` (namespace Apple). El
    parseo de respuestas multistatus usa `QXmlStreamReader`.
    `fetchEvents()` hace REPORT `calendar-query` (filtro VEVENT) devolviendo
    `{href, etag, calendar-data}` por evento. `putEvent()`/`deleteEvent()`
    hacen PUT (con `If-Match` si hay etag, recurso `<uid>.ics`) y DELETE.
  - `CalendarManager::addCalDavAccount(serverUrl, username, password)`:
    descubre los calendarios, guarda credenciales en KWallet, crea un
    `LocalCalendar` tipo "caldav" por cada calendario remoto descubierto y
    lanza un `syncCalendar` inicial para cada uno. Emite
    `calDavAccountAdded(accountId, calendarCount, error)`.
  - `CalendarManager::syncCalendar(calendarId)` / `syncAll()`: pull vía
    `fetchEvents`; por cada evento remoto compara `etag` con el guardado, si
    cambió reemplaza el evento local (clon) y actualiza `syncItems`; los UID
    conocidos que ya no aparecen en remoto se borran localmente. Emite
    `syncStarted`/`syncFinished`/`syncError(calendarId, error)`.
    **Excepciones de recurrencia**: un recurso `.ics` puede contener el VEVENT
    maestro más sus VEVENT `RECURRENCE-ID` (instancias modificadas de una
    serie recurrente); ahora se procesan todas: el maestro se trata como
    antes (clon, `syncItems` por uid) y cada excepción se clona y se añade al
    `MemoryCalendar` con el mismo uid + `recurrenceId()`. Si el recurso
    cambia de etag o desaparece, se borran también las instancias existentes
    vía `Calendar::deleteEventInstances()`. `OccurrenceIterator` (usado por
    `EventListModel`) sustituye automáticamente la ocurrencia correspondiente
    por la excepción, sin cambios en `eventlistmodel.cpp`.
  - Push-on-edit: `addEvent`/`updateEvent`/`rescheduleEvent`/
    `moveEventToCalendar` llaman a `pushEvent` (serializa con `ICalFormat` y
    hace PUT, actualizando `syncItems`) y `removeEvent`/`moveEventToCalendar`
    (en el calendario origen) llaman a `pushDelete` (DELETE remoto) para
    calendarios tipo "caldav". Sincronización v1 = "last write wins", sin UI
    de resolución de conflictos. **Edición de instancias individuales de una
    serie recurrente desde Kweek no está soportada** (el diálogo de edición
    opera sobre el evento maestro vía uid).
  - **Pending-push / reintentos** (`LocalCalendar::pendingPush`, persistido
    en `.../calendars/<id>.sync.json` como array `pendingPush`): al editar,
    crear, posponer o mover un evento de un calendario "caldav"/"google", su
    uid se marca como pendiente (`markPendingPush`) antes de `pushEvent`; se
    desmarca al recibir confirmación del servidor (`eventPut`). Si el push
    falla, el uid queda pendiente: el siguiente `syncCalendar`/`syncAll`
    **no sobrescribe esa entrada local con la versión remota** (evita perder
    la edición local por una sincronización en segundo plano) y, al terminar
    el pull, `retryPendingPushes()` reintenta el push automáticamente.
  - `src/qml/AddCalDavAccountDialog.qml` — diálogo (Kirigami.Dialog) con
    presets de proveedor (iCloud/Fastmail/Nextcloud/Custom), campos de
    servidor/usuario/contraseña de aplicación, indicador de progreso y
    mensaje de error/éxito (vía señal `calDavAccountAdded`).
  - `src/qml/CalendarSidebar.qml` añade botón "Connect CalDAV account…" que
    abre el diálogo anterior, y por cada calendario CalDAV un botón de
    sincronización manual (`syncCalendar`) con indicador de progreso
    (`syncStarted`/`syncFinished`/`syncError`).
- **Google Calendar (OAuth2 + API v3)**: `CalendarManager` gestiona también
  **calendarios Google** (`type: "google"`), persistidos igual que los CalDAV
  (`accountId` + `remoteUrl` = id de calendario de Google) más `syncToken`
  (sync incremental) en `.../calendars/<id>.sync.json`. Cuentas en
  `accounts.json` ganan campo `type` ("caldav"/"google"; sin `type` se asume
  "caldav"). Credenciales Google (`email` + `refreshToken`) en KWallet vía
  `CredentialStore::storeGoogleTokens`/`readGoogleTokens`.
  - `src/core/googleoauthconfig.h` (gitignored, plantilla en
    `googleoauthconfig.h.example`): `kClientId`/`kClientSecret` del OAuth
    client "Desktop app" de Google Cloud Console.
  - `src/core/googlecalendarclient.h/.cpp` — `GoogleCalendarClient`:
    `authenticate()` ejecuta el flujo `QOAuth2AuthorizationCodeFlow` (PKCE
    S256) con `QOAuthHttpServerReplyHandler` (puerto local efímero),
    `access_type=offline`+`prompt=consent` para forzar `refresh_token`, y
    scopes `calendar`+`email`; al recibir `granted()` consulta
    `userinfo` para el email y emite `authenticated(refreshToken, email,
    error)`. También conecta `QAbstractOAuth2::serverReportedErrorOccurred` y
    `QAbstractOAuth::requestFailed` para reportar como error cualquier fallo
    del flujo OAuth2 (p.ej. `invalid_client` por client secret incorrecto, o
    403 por tener la Google Calendar API deshabilitada en el proyecto de
    Google Cloud) en lugar de quedarse bloqueado en "Connecting…" sin
    feedback. El resto de operaciones (`listCalendars`, `fetchEvents`,
    `putEvent`, `deleteEvent`) cambian el `refreshToken` por un access token
    fresco vía `POST /token` antes de cada llamada (`withAccessToken`).
    `fetchEvents(calendarId, syncToken)` usa `singleEvents=false`; un 410
    (syncToken inválido) se reporta como `syncTokenInvalid=true` para forzar
    resync completo. Pagina automáticamente vía `fetchEventsPage()`
    (recursiva, sigue `nextPageToken` acumulando resultados) hasta que la
    respuesta no tenga `nextPageToken`, momento en el que se usa su
    `nextSyncToken`; necesario porque Google devuelve como máximo 250 eventos
    por página y calendarios grandes (p.ej. "Oscar Privado", >250 eventos)
    perdían silenciosamente los eventos de páginas siguientes (incl. eventos
    futuros) antes de este fix.
  - Mapeo evento <-> JSON de Google: `googleJsonToEvent()`/
    `eventToGoogleJson()` en `calendarmanager.cpp` (summary, description,
    location, start/end con `date` todo el día o `dateTime`+`timeZone`,
    `transparency` opaque/transparent <-> busy/free, `recurrence:
    ["RRULE:FREQ=..."]` simple con soporte de `UNTIL`/`COUNT` (helpers
    `rruleUntil()`/`rruleCount()`, aplicados vía
    `Recurrence::setEndDateTime()`/`setDuration()`; antes se descartaban,
    convirtiendo eventos recurrentes finitos de Google en recurrencias
    infinitas que aparecían también "hoy"), `reminders.overrides` <-> alarma
    Display).
  - `CalendarManager::addGoogleAccount()`: autentica vía navegador, guarda
    tokens, hace `listCalendars` y crea un `LocalCalendar` tipo "google" por
    calendario descubierto, lanzando `syncCalendar` inicial. Emite
    `googleAccountAdded(accountId, calendarCount, error)`.
  - `syncCalendar(id)` despacha a `syncCalDavCalendar`/`syncGoogleCalendar`
    según `type`. `syncGoogleCalendar` usa `syncToken` para pull
    incremental; eventos `status: "cancelled"` se borran localmente; al
    recibir `syncTokenInvalid` limpia `syncToken`+`syncItems` y repite con
    resync completo.
    **Excepciones de recurrencia**: los items con `recurringEventId` (instancia
    modificada/cancelada de una serie) se procesan en una segunda pasada
    (tras procesar maestros/eventos normales, para que el maestro ya esté en
    el `MemoryCalendar` si llega en el mismo lote). `googleJsonToExceptionEvent()`
    construye el evento de excepción reutilizando `googleJsonToEvent()`, le
    asigna `uid = recurringEventId` (mismo uid que el maestro) y
    `recurrenceId` a partir de `originalStartTime` (helper
    `googleOriginalStartTime()`); se guarda con clave compuesta
    `uid#recurrenceIdISO` en `syncItems` (valor = `{googleEventId, etag}`).
    Si el item viene con `status: "cancelled"`, en vez de añadir una
    excepción se llama a `master->recurrence()->addExDateTime(recurrenceId)`
    sobre el evento maestro (si existe localmente), excluyendo esa ocurrencia
    de la serie (equivalente a `EXDATE`). **Limitación v1 restante**: si una
    excepción conocida desaparece de remoto sin llegar como `cancelled`
    (p.ej. al "deshacer" la modificación de una instancia), no se borra
    localmente; y editar/mover/borrar una instancia individual desde Kweek no
    está soportado (ver nota en la sección CalDAV).
  - `pushEvent`/`pushDelete` ganan rama `type == "google"`: al crear un
    evento, Google asigna su propio id y el UID local se renombra para que
    coincida (clonando el evento con el nuevo UID).
  - `src/qml/AddGoogleAccountDialog.qml` — diálogo con botón "Sign in with
    Google" + estado/progreso (señal `googleAccountAdded`).
    `CalendarSidebar.qml` añade botón "Connect Google account…" y extiende
    el botón/indicador de sync manual a calendarios `type: "google"`.
- **Microsoft Graph (OAuth2 + API v1.0)**: `CalendarManager` gestiona también
  **calendarios Microsoft** (`type: "microsoft"`), persistidos igual que los
  Google (`accountId` + `remoteUrl` = id de calendario de Graph). El campo
  `syncToken` se reutiliza para guardar la **URL completa de
  `@odata.deltaLink`** (el equivalente de Graph al `syncToken` de Google).
  Credenciales (`email` + `refreshToken`) en KWallet vía
  `CredentialStore::storeMicrosoftTokens`/`readMicrosoftTokens`.
  - `src/core/microsoftoauthconfig.h` (gitignored, plantilla en
    `microsoftoauthconfig.h.example`): `kClientId` de un **app registration de
    Azure AD** ("Accounts in any organizational directory and personal
    Microsoft accounts", plataforma "Mobile and desktop applications",
    redirect `http://localhost`, "Allow public client flows" = Yes). Cliente
    público (sin secreto), solo PKCE.
  - `src/core/microsoftgraphclient.h/.cpp` — `MicrosoftGraphClient`: cliente
    sobre `QNetworkAccessManager`. `authenticate()` ejecuta
    `QOAuth2AuthorizationCodeFlow` (PKCE S256) contra
    `login.microsoftonline.com/common/oauth2/v2.0/{authorize,token}` con
    scopes `Calendars.ReadWrite`, `User.Read`, `offline_access`, `openid`,
    `email`; al recibir `granted()` consulta `https://graph.microsoft.com/v1.0/me`
    para el email (`mail`, fallback `userPrincipalName`) y emite
    `authenticated(refreshToken, email, error)`. Igual que Google, conecta
    `serverReportedErrorOccurred`/`requestFailed` para reportar errores de
    OAuth2 en lugar de quedarse en "Connecting…". El resto de operaciones
    (`listCalendars`, `fetchEvents`, `putEvent`, `deleteEvent`) cambian el
    `refreshToken` por un access token fresco vía `POST /token` antes de cada
    llamada (`withAccessToken`, sin `client_secret`: cliente público). Todas
    las peticiones llevan la cabecera `Prefer: outlook.timezone="UTC"`, de
    forma que Graph devuelve/acepta siempre `start`/`end` en UTC (evita el
    mapeo nombre-de-zona-horaria-de-Windows <-> IANA). `listCalendars()` hace
    GET `/me/calendars` (`id`->id, `name`->displayName,
    `isDefaultCalendar`->primary, `color` es un enum con nombre tipo
    `lightBlue`/`auto`/... mapeado a hex aproximado vía tabla en
    `graphColorToHex()`, `auto`/desconocido -> cadena vacía -> color por
    defecto). `fetchEvents(calendarId, deltaLink)`: si `deltaLink` está vacío
    hace GET `/me/calendars/{id}/events/delta`, si no GET directo a la URL de
    `deltaLink`; pagina vía `fetchEventsPage()` siguiendo `@odata.nextLink`
    hasta que la respuesta trae `@odata.deltaLink` (emitido como
    `nextDeltaLink`); cada item -> `{id, etag: "@odata.etag", json,
    removed: contiene "@removed"}`; un 410 se reporta como
    `deltaInvalid=true` para forzar resync completo. `putEvent()` hace POST
    (crear, `eventId` vacío) o PATCH (actualizar) a `/me/calendars/{id}/events[/eventId]`,
    devolviendo `id`+`@odata.etag`. `deleteEvent()` hace DELETE; un 404 se
    trata como éxito.
  - Mapeo evento <-> JSON de Graph: `microsoftJsonToEvent()`/
    `eventToMicrosoftJson()` en `calendarmanager.cpp` (`id`->uid,
    `subject`->summary, `body.content`->description (HTML tal cual, sin
    limpiar), `location.displayName`->location, `start`/`end` siempre como
    `{"dateTime": ..., "timeZone": "UTC"}` gracias a la cabecera `Prefer`,
    `isAllDay`, `showAs` free/busy <-> `Transparent`/`Opaque`. Recurrencia:
    solo `daily`/`weekly`/`absoluteMonthly`/`absoluteYearly` con
    `interval == 1` (cualquier otro patrón, incl. `relativeMonthly`/
    `relativeYearly` o `interval != 1`, se importa **sin recurrencia** —
    limitación v1 documentada); `range.type` `endDate`/`numberOfOccurrences`/
    `noEnd` <-> `setEndDateTime()`/`setDuration()`. Al exportar, `weekly`
    incluye `daysOfWeek` (vía `graphDayOfWeek()`) y `absoluteMonthly`/
    `absoluteYearly` incluyen `dayOfMonth`/`month` (campos requeridos por
    Graph que Google no exige). `isReminderOn`+`reminderMinutesBeforeStart`
    <-> una alarma Display, igual que Google.
  - `CalendarManager::addMicrosoftAccount()`: autentica vía navegador, guarda
    tokens, hace `listCalendars` y crea un `LocalCalendar` tipo "microsoft"
    por calendario descubierto, lanzando un `syncCalendar` inicial. Emite
    `microsoftAccountAdded(accountId, calendarCount, error)`.
  - `syncCalendar(id)` despacha también a `syncMicrosoftCalendar` para
    `type == "microsoft"`. `syncMicrosoftCalendar` usa el `@odata.deltaLink`
    guardado en `syncToken` para pull incremental; items con `"@removed"` se
    borran localmente; un `deltaInvalid` limpia `syncToken`+`syncItems` y
    repite con resync completo. **Limitación v1**: los items con
    `type: "occurrence"`/`"exception"` (instancias modificadas/canceladas de
    una serie recurrente) se **omiten** en el pull — a diferencia de Google,
    las excepciones de recurrencia de Microsoft todavía no se sincronizan.
  - `pushEvent`/`pushDelete` ganan rama `type == "microsoft"`, idéntica en
    estructura a la de Google: al crear un evento, Graph asigna su propio id
    y el UID local se renombra para que coincida.
  - `src/qml/AddMicrosoftAccountDialog.qml` — diálogo con botón "Sign in with
    Microsoft" + estado/progreso (señal `microsoftAccountAdded`).
    `CalendarSidebar.qml` añade botón "Connect Microsoft account…" y extiende
    el botón/indicador de sync manual a calendarios `type: "microsoft"`.

  **Estado de verificación**: build limpio (`cmake -B build -G Ninja &&
  cmake --build build`) y smoke test offscreen sin errores QML. No se ha
  probado un login real (requiere un app registration de Azure AD propio,
  ver `microsoftoauthconfig.h.example`); pendiente verificación end-to-end
  con una cuenta Microsoft real (login, descubrimiento, sync inicial/
  incremental, push/borrado de eventos), igual que se hizo para Google.
- `src/core/eventlistmodel.h/.cpp` — `EventListModel` (QAbstractListModel,
  QML_ELEMENT): expande ocurrencias (incl. recurrentes) de todos los
  calendarios locales **visibles** dentro de `[rangeStart, rangeEnd)`
  usando `OccurrenceIterator` por calendario, fusionadas y ordenadas por
  inicio. Roles: `uid`, `summary`, `description`, `location`, `start`,
  `end`, `allDay`, `color` (override del evento o color del calendario),
  `recurring`, `recurrence`, `reminderMinutes`, `busy`, `calendarId`.
- `src/qml/CalendarSidebar.qml` — panel lateral izquierdo con la lista de
  calendarios (`CalendarManager.calendars`): checkbox de visibilidad,
  círculo de color (clic abre selector de color con paleta de 8 colores),
  campo de texto editable para renombrar, botón de borrar (oculto si solo
  queda un calendario) y botón "Nuevo calendario".
- `src/qml/CalendarPage.qml` — página principal: `CalendarSidebar` a la
  izquierda + separador, y a la derecha cabecera con
  Hoy/Anterior/Siguiente/Nuevo evento (Anterior/Siguiente avanzan por mes,
  semana o día según la vista activa), selector de vista
  (Agenda/Mes/Semana/Día) y título dinámico (mes/año, rango de semana o
  fecha completa). Doble clic en un día de la vista mes, o clic en la
  cabecera de un día en la vista semana, abre la vista de día de ese día.
- `src/qml/AgendaView.qml` — lista semanal de eventos (alta/edición/borrado,
  posponer +1h/+1día).
- `src/qml/MonthView.qml` — cuadrícula mensual (6x7), chips de eventos por
  día (máx. 3 + "+N more"), clic en día = nuevo evento, doble clic = ir a
  vista semana de ese día. Los chips se pueden arrastrar y soltar sobre otro
  día (`Drag`/`DropArea`) para mover el evento conservando la hora
  (`rescheduleEvent`).
- `src/qml/WeekView.qml` — rejilla horaria de 24h por 7 días, fila separada
  para eventos de todo el día, línea roja de "ahora" en el día actual,
  eventos solapados se reparten en columnas. Cada bloque de evento se puede
  arrastrar (mover día/hora, snap a 15 min, `rescheduleEvent`) o redimensionar
  desde su borde inferior (cambia solo la hora de fin, `updateEvent`); un
  clic simple sigue abriendo el diálogo de edición.
- `src/qml/DayView.qml` — vista de día: cabecera con el día completo
  (`dddd, d MMMM yyyy`), fila de eventos de todo el día, y rejilla horaria
  de 24h de una sola columna con la misma interacción que la vista de
  semana (arrastrar para retemporizar con snap a 15 min vía
  `rescheduleEvent`, asa inferior para redimensionar vía `updateEvent`,
  línea de "ahora" si es hoy).
- `src/qml/EventEditDialog.qml` — diálogo de alta/edición compartido por las
  tres vistas (`CalendarManager.eventData(uid)` rellena el formulario al
  editar). Incluye selector de calendario destino (`ComboBox` sobre
  `CalendarManager.calendars`; al cambiarlo en edición llama a
  `moveEventToCalendar`) y un checkbox "usar color del calendario" que, si
  se desmarca, muestra la paleta de colores de override por evento.
- `src/qml/DateUtils.js` — utilidades de fechas (semana empieza en lunes).

Verificado: compila limpio y se ejecuta sin errores QML
(`cmake -B build -G Ninja && cmake --build build`); probado end-to-end
(add/reschedule/update, incl. eventos recurrentes y de todo el día)
inspeccionando el `.ics` resultante. No se pudo tomar captura visual en
este entorno (la ventana no aparecía en `spectacle`), pendiente de
verificación visual manual.

CalDAV: build limpio con las nuevas dependencias (`Qt6::Network`,
`KF6::Wallet`) y smoke test offscreen sin errores QML. No se ha podido probar
una sincronización real contra iCloud/Nextcloud/Fastmail en este entorno (sin
credenciales); pendiente de verificación manual con una cuenta real
(descubrimiento de calendarios, pull/push de eventos, borrado de cuenta).

Google Calendar: **verificado end-to-end con una cuenta real**
(neldoreth@gmail.com). Login interactivo vía navegador (Firefox) con el flujo
PKCE completo, descubrimiento automático de las 7 cuentas/calendarios de
Google del usuario (incl. "Festivos en España" y varios calendarios
compartidos/de familia), creación de un `LocalCalendar` tipo "google" por
cada uno y sync inicial completo (eventos reales descargados a sus `.ics` +
`.sync.json` correspondientes), todo sin errores. Notas del proceso:

- El client ID/secret de OAuth2 deben pertenecer a un proyecto de Google
  Cloud con la **Google Calendar API habilitada** (Biblioteca de APIs); si no
  lo está, `listCalendars` falla con HTTP 403
  (`accessNotConfigured`/`PERMISSION_DENIED`).
- Mientras la pantalla de consentimiento OAuth esté en modo "Testing", solo
  pueden iniciar sesión los correos añadidos como "Usuarios de prueba" (si
  no, Google devuelve "Error 403: access_denied" antes de llegar a la
  pantalla de permisos).
- Un `client_secret` incorrecto produce `invalid_client` (HTTP 401) en el
  intercambio del código por el token; Qt lo reporta como
  `QNetworkReply::AuthenticationRequiredError` ("server requires
  authentication") — de ahí las señales `serverReportedErrorOccurred`/
  `requestFailed` añadidas a `GoogleCalendarClient::authenticate()` para que
  esto se vea en el diálogo en lugar de quedarse en "Connecting…".

Sync incremental con `syncToken`, push/borrado de eventos individuales y
borrado de cuenta ya verificados, ver más abajo.

**Fixes post-verificación (mismo día, misma cuenta real)**:

- Eventos recurrentes finitos de Google (p.ej. `RRULE:FREQ=WEEKLY;UNTIL=...`
  o `;COUNT=...`) se importaban como recurrencia infinita (`FREQ=WEEKLY` sin
  fin), por lo que aparecían también "hoy" años después de haber terminado
  (detectado con eventos reales de 2013/2022/2023 apareciendo en junio de
  2026). Corregido en `googleJsonToEvent()`/`eventToGoogleJson()` (ver
  arriba); verificado end-to-end forzando un resync completo (borrando
  `.ics`/`.sync.json` del calendario y reiniciando la app) en los calendarios
  "mari.filiu@gmail.com" y "Niños".
- El calendario "Oscar Privado" (>250 eventos) no mostraba eventos futuros
  recientes (p.ej. "Vacuna polvo" del día siguiente): `fetchEvents` solo leía
  la primera página (250 eventos) de Google y descartaba `nextPageToken`.
  Corregido con `fetchEventsPage()` recursiva (ver arriba); verificado:
  tras el fix el resync trae 2769 eventos (antes 250) y el evento del día
  siguiente aparece correctamente.
- **Nota de depuración importante**: editar a mano `~/.local/share/Kweek/Kweek/calendars.json`/`.sync.json`/`.ics` mientras la
  app está en ejecución NO tiene efecto — `CalendarManager` mantiene su
  propio estado en memoria (`syncToken`/`syncItems`/`MemoryCalendar`) cargado
  al inicio y lo vuelve a escribir en cada sync, sobrescribiendo cualquier
  cambio externo. Para forzar un resync completo de un calendario hay que:
  cerrar la app, borrar `calendars/<id>.ics`, `.ics~` y `.sync.json`, y
  relanzar la app **antes** de pulsar sync.

**Auditoría del resto de calendarios Google de la cuenta** (mismo día,
comparando recuento de eventos vía API de Google con paginación contra
`.ics`/`.sync.json` locales):

- "Festivos en España" (`301f1963-846b-4581-8d81-4903105743c5`, >250 eventos)
  tenía el mismo bug de paginación que "Oscar Privado" (250 locales vs 386 en
  Google). Corregido con el mismo procedimiento de resync completo; verificado
  386/386 tras el resync.
- "ivanbernabeuperez@gmail.com" tenía 30 eventos locales vs 31 en Google; el
  evento que faltaba era una **excepción de recurrencia** (`recurringEventId`
  presente, instancia "Revisar Mac de VCS" del 2022-03-22 con
  `RECURRENCE-ID`). Tras implementar el soporte de excepciones (ver sección
  Google Calendar arriba) y forzar un resync completo, ahora son 31/31:
  verificado end-to-end, incluyendo el `RECURRENCE-ID`/`UID` correcto en el
  `.ics` resultante y la clave compuesta `uid#recurrenceIdISO` en
  `.sync.json`.
- "Cine y TV" (106/106) y "Familia" (15/15) coinciden exactamente con Google,
  sin problemas.
- Con esto, los 7 calendarios Google de la cuenta (Festivos en España,
  ivanbernabeuperez@gmail.com, mari.filiu@gmail.com, Cine y TV, Niños, Oscar
  Privado, Familia) están verificados como correctos.

**Instancia cancelada de una serie recurrente (`status: "cancelled"` +
`recurringEventId` -> `EXDATE` en el maestro) — verificado end-to-end**
(2026-06-13, cuenta real, calendario "Cine y TV"): usando el mismo harness
`--sync-test <calendarId>` (revertido tras la prueba) y la API de Google
Calendar para crear/borrar eventos de prueba:

- Se creó un evento recurrente real "EXC-TEST recurring"
  (`RRULE:FREQ=WEEKLY;COUNT=4`, desde 2026-06-15).
- Se borró solo la 2ª instancia (2026-06-22) vía la API de Google
  (`DELETE` sobre el id de instancia `<masterId>_20260622T080000Z`), lo que
  Google reporta como un evento separado con `status: "cancelled"` +
  `recurringEventId` + `originalStartTime`.
- Tras un `syncCalendar()`, el `.ics` resultante para el evento maestro
  contiene `RRULE:FREQ=WEEKLY;COUNT=4` **y**
  `EXDATE;TZID=Europe/Madrid:20260622T100000`, confirmando que
  `master->recurrence()->addExDateTime(recurrenceId)` se aplicó
  correctamente y que la ocurrencia cancelada no aparece como excepción
  separada en `syncItems` (solo la entrada del maestro).
- Limpieza: se borró el evento maestro completo en Google y se repitió el
  sync; el evento de prueba desapareció por completo del `.ics` local
  (0 referencias a "EXC-TEST").

Con esto, el camino "instancia cancelada -> `EXDATE`" queda verificado.

**Reintento automático de `pendingPush` tras un push fallido — verificado
end-to-end** (2026-06-13): usando un entorno aislado (`XDG_DATA_HOME`
apuntando a un directorio temporal, sin tocar la cuenta real) con un
`calendars.json`/`accounts.json` ficticios (un calendario "Personal" local +
un calendario `type: "caldav"` `pp-test-cal` con `accountId:
"pp-test-account"` y `remoteUrl: http://127.0.0.1:8123/cal/`), credenciales
falsas en KWallet, un servidor CalDAV mock (`http.server` de Python, REPORT
-> multistatus vacío 207, PUT configurable 500/201) y un harness temporal
(`--pending-push-test create|retry <calendarId>` en `main.cpp`, revertido tras
la prueba):

- **Fase "create"** (mock con PUT -> 500): `addEvent()` marca el uid en
  `pendingPush` y `pushEvent()` hace `PUT`, que falla (500 ->
  `QNetworkReply` error). Resultado: se emite `syncError`, y
  `pp-test-cal.sync.json` queda con `"pendingPush": ["<uid>"]` y `"items":
  {}` (no se pierde el evento local ni se marca como sincronizado).
- **Fase "retry"** (mock con PUT -> 201 + ETag): `syncCalendar()` hace
  `REPORT` (multistatus vacío, sin cambios remotos) y al terminar llama a
  `retryPendingPushes()`, que reintenta `pushEvent()` para el uid pendiente;
  el `PUT` esta vez devuelve 201, `eventPut` actualiza `syncItems` con el
  nuevo etag y elimina el uid de `pendingPush`. Resultado:
  `pp-test-cal.sync.json` queda con `"items": {"<uid>": {"etag":
  "\"etag-ok-1\"", "href": "..."}}` y `"pendingPush": []`.

Con esto, el mecanismo de pending-push (no se pierde una edición local si el
push falla, y se reintenta automáticamente en el siguiente sync hasta tener
éxito) queda verificado end-to-end. Las credenciales y ficheros de prueba se
eliminaron tras la prueba.

**Sync incremental con `syncToken` — verificado end-to-end** (2026-06-13,
cuenta real, calendario "Cine y TV"): usando un harness temporal
(`--sync-test <calendarId>` en `main.cpp`, revertido tras la prueba) que
llama directamente a `syncCalendar()` y un `qWarning` temporal con el
resultado de `eventsFetched`:

- Sync sin cambios: `events: 0`, `syncTokenInvalid: false`, `nextSyncToken`
  idéntico al guardado (no se re-descargan los eventos existentes).
- `syncTokenInvalid: true` (token guardado caducado, camino que antes no se
  había probado): se limpia `syncToken`+`syncItems` y se repite con resync
  completo automáticamente; tras el resync se guarda un `syncToken` nuevo.
- Evento añadido en Google -> tras el resync completo aparece en el `.ics`
  local (106 -> 107 eventos).
- Evento borrado en Google -> la siguiente sync incremental devuelve
  `events: 1` con `status: cancelled`; el evento se borra del `.ics` local
  (107 -> 106) y se guarda un `syncToken` nuevo.

Con esto, sync incremental (incl. el fallback por token caducado, alta y baja
de eventos) queda verificado.

**Push/borrado de eventos individuales (Kweek -> Google) — verificado
end-to-end** (2026-06-13, cuenta real, calendario "Cine y TV"): usando un
harness temporal (`--push-test create|update|delete <calendarId> [uid]` en
`main.cpp`, revertido tras la prueba) que llama directamente a
`addEvent`/`updateEvent`/`removeEvent`:

- **Crear**: `addEvent()` crea el evento local, `pushEvent()` hace `PUT` a
  Google (sin `googleEventId` -> Google asigna uno nuevo), y el UID local se
  renombra al id devuelto por Google (`eventPut` con `eventId` distinto del
  `uid` original). Verificado con `get_event` en Google: evento creado con el
  resumen/descripción/horario correctos.
- **Actualizar**: `updateEvent()` + `pushEvent()` hace `PUT` con el
  `googleEventId` ya conocido (de `syncItems`) y guarda el `etag` nuevo.
  Verificado: el `summary` cambia en Google y `updated` se actualiza.
- **Borrar**: `removeEvent()` + `pushDelete()` hace `DELETE` en Google y quita
  la entrada de `syncItems`. Verificado: `get_event` devuelve `status:
  cancelled`.

**Borrado de cuenta Google — verificado** (2026-06-13): `removeCalendar()`
ya gestiona el caso CalDAV/Google de forma genérica (ver código): si al
borrar un calendario era el último de su `accountId`, borra también el
`Account` de `accounts.json` y llama a
`CredentialStore::removeCredentials(accountId)`. Para verificarlo sin tocar
la cuenta real (que tiene 7 calendarios y borrarlos todos sería
destructivo/no reversible sin volver a hacer login+resync completo), se usó
un entorno aislado (`XDG_DATA_HOME` apuntando a un directorio temporal, más
un harness temporal `--account-delete-test` en `main.cpp`, todo revertido
tras la prueba) con un `calendars.json`/`accounts.json` ficticios (un
calendario "Personal" local + un calendario `type: "google"` con
`accountId: "test-google-account-id"`, y credenciales falsas guardadas vía
`storeGoogleTokens`). Resultado tras `removeCalendar("test-google-cal-1")`:
calendario y `.ics`/`.sync.json` eliminados, entrada de `accounts.json`
eliminada, y credenciales borradas de KWallet.

Esta prueba destapó **un bug real en `CredentialStore`** (ya corregido en
`src/core/credentialstore.cpp`):

- `readMap()` no comprobaba `wallet->hasEntry(key)` antes de llamar a
  `wallet->readMap()`; KWallet devuelve `0` (éxito) con un mapa vacío incluso
  para una entrada inexistente, por lo que `readCredentials`/
  `readGoogleTokens` devolvían `true` con `username`/`password` o
  `email`/`refreshToken` vacíos para una cuenta ya borrada (o nunca
  existente) en lugar de `false`. Corregido añadiendo la comprobación
  `hasEntry()`.
- `removeCredentials()` no llamaba a `wallet->sync()`: `removeEntry()`
  surtía efecto en la conexión KWallet en curso (`hasEntry()` ya devolvía
  `false` inmediatamente después), pero sin `sync()` el cambio no se
  persistía antes de cerrar la conexión, y una `openWallet()` posterior
  (p.ej. al comprobar credenciales) volvía a ver la entrada "borrada".
  Corregido añadiendo `wallet->sync()` tras `removeEntry()`.

Con esto, la integración Google Calendar queda completamente verificada
end-to-end (login, descubrimiento, sync inicial con paginación, sync
incremental con `syncToken` + fallback por token caducado, push/borrado de
eventos individuales y borrado de cuenta).

**Excepciones de recurrencia y pending-push — implementado (2026-06-13)**:
ver detalles en las secciones CalDAV y Google Calendar arriba
(`googleJsonToExceptionEvent`/`googleOriginalStartTime`, manejo de
`RECURRENCE-ID`/`recurringEventId`/`EXDATE`, y
`LocalCalendar::pendingPush`/`markPendingPush`/`retryPendingPushes`).
Verificado end-to-end con la cuenta real: resync completo de
"ivanbernabeuperez@gmail.com" pasa de 30/31 a 31/31 eventos, con la
excepción correctamente serializada (`RECURRENCE-ID`, mismo `UID` que el
maestro) y registrada en `.sync.json` con clave compuesta
`uid#recurrenceIdISO`. El caso "instancia cancelada" (`EXDATE` sobre el
maestro) y el reintento de `pendingPush` tras un push fallido también están
verificados end-to-end (ver más abajo, secciones "Instancia cancelada de una
serie recurrente" y "Reintento automático de `pendingPush`").

Nota de CMake: fue necesario añadir `target_include_directories(kweek
PRIVATE core)` para que la generación automática de `qmltyperegistrations`
encuentre `calendarmanager.h`/`eventlistmodel.h` por nombre simple.

Dependencias de desarrollo necesarias (Arch): `cmake`, `extra-cmake-modules`,
`ninja` (o `make`), Qt6 (`qtbase`, `qtdeclarative`, `qtnetworkauth` no usado
aún), `kirigami2`/`kirigami` (KF6), `kcoreaddons`, `ki18n`, `kcalendarcore`,
`kwallet`.

Este documento sirve como plan de referencia y hoja de ruta para retomar el
trabajo entre sesiones.

---

## Inspiración: características clave de BusyCal / Fantastical a replicar

- Vistas: día, semana, mes, año y lista de agenda, con cambio rápido entre ellas.
- Mini-calendario de navegación (selector de mes).
- Panel de detalle de evento con todos los campos editables in-place.
- Edición rápida: arrastrar para mover/redimensionar eventos (cambiar
  hora/duración), arrastrar entre días.
- "Posponer / adelantar" eventos: acciones rápidas (+15 min, +1 h, +1 día,
  "mañana", etc.) y soporte de snooze para recordatorios.
- Colores personalizables por calendario (y posibilidad de override de color
  por evento).
- Vista combinada de múltiples calendarios/cuentas con checkboxes de
  visibilidad por calendario.
- Detección de enlaces de videollamada (Zoom/Meet/Teams) en la descripción/ubicación
  con botón de "unirse".
- Soporte de zonas horarias por evento.
- Indicador de tiempo/temperatura integrado en la interfaz (icono pequeño,
  configurable).
- Búsqueda de eventos.
- Recordatorios/alertas múltiples por evento.

---

## Stack tecnológico (propuesta)

| Área | Elección | Motivo |
|---|---|---|
| UI | Qt 6 + QML + Kirigami | Integración nativa KDE, look moderno, buen soporte Flatpak |
| Lenguaje | C++ (backend) + QML (UI) | Rendimiento, ecosistema KDE Frameworks |
| Modelo de datos / iCal | KCalendarCore | Parseo iCalendar, RRULE, recurrencias, ya validado por KDE |
| Caché local | SQLite | Almacenamiento offline de eventos y cuentas |
| Auth OAuth | Qt Network Authorization (QOAuth2AuthorizationCodeFlow) | Login moderno Google/Microsoft con PKCE |
| Empaquetado | Flatpak (runtime org.kde.Platform) | Distro-agnóstico, fácil instalación |

### Backends de sincronización

- **CalDAV genérico**: para Apple iCloud (con contraseña de aplicación,
  ya que iCloud CalDAV no soporta OAuth de terceros), Nextcloud, Fastmail,
  cualquier servidor CalDAV estándar.
- **Google Calendar API**: OAuth2 (PKCE) + Google Calendar REST API v3.
- **Microsoft Graph API**: OAuth2 (cuentas personales Microsoft y
  Microsoft 365 / trabajo-escuela) vía MSAL/Graph, soporte multi-tenant.
- **Apple Calendar**: vía CalDAV (icloud.com), igual que iCloud genérico.

Todos los backends deben mapear al máximo de campos posibles del modelo
iCalendar (ver sección "Modelo de datos").

---

## Modelo de datos de evento (campos objetivo)

Campos a soportar y sincronizar siempre que el proveedor lo permita:

- Título / resumen
- Descripción / notas
- Ubicación (texto + geolocalización si disponible)
- Fecha/hora de inicio y fin, soporte "todo el día" y multi-día
- Zona horaria por evento
- Recurrencia (RRULE completa: diaria, semanal, mensual, anual, personalizada,
  excepciones)
- Recordatorios/alertas múltiples (minutos/horas/días antes, por notificación)
- Estado: confirmado / tentativo / cancelado
- Disponibilidad: ocupado / libre
- Visibilidad: público / privado
- Asistentes (con estado de aceptación: aceptado/declinado/pendiente)
- Organizador
- URL / enlace (incl. detección de videollamadas)
- Adjuntos
- Categorías / etiquetas
- Color (heredado del calendario o sobrescrito por evento)
- Calendario al que pertenece (y cuenta asociada)
- Última modificación / metadatos de sincronización (UID, ETag, etc.)

### Acciones rápidas sobre eventos

- Posponer (+15 min, +30 min, +1 h, +1 día, próxima semana, personalizado)
- Adelantar (mismas opciones, hacia atrás)
- Duplicar evento
- Mover a otro calendario
- Snooze de recordatorios

---

## Gestión de calendarios y cuentas

- Número ilimitado de cuentas/calendarios.
- Tipos de cuenta soportados: Google, Microsoft (personal y 365/trabajo),
  Apple/iCloud (CalDAV), CalDAV genérico, calendario local.
- Por calendario: nombre, color (selector de color libre), visibilidad
  (mostrar/ocultar), permisos de escritura.
- Gestión de cuentas integrada con KWallet para almacenamiento seguro de
  tokens/credenciales.

---

## Widget de tiempo (clima)

- Icono pequeño de tiempo + temperatura, **desactivado por defecto**,
  activable desde Ajustes.
- Selección de proveedor meteorológico, con foco en España:
  - **Open-Meteo** (gratuito, sin API key, buena cobertura global) — opción
    por defecto recomendada.
  - **AEMET OpenData** (oficial España, requiere API key gratuita) — opción
    alternativa para mayor precisión en España.
- Selector de ciudad/ubicación manual (no depender solo de geolocalización).
- Mostrar previsión integrada en las vistas de día/semana (icono+temp por día).

---

## Identidad visual / Icono de la app

- Icono inicial creado: `icons/io.github.neldoreth.Kweek.svg`. Página de
  calendario con anillos de carpeta, banda superior naranja, fila de puntos
  representando la semana (destacando el día actual) y número de día grande,
  sobre fondo degradado morado→turquesa.
- Pendiente: generar variantes para distintos tamaños/temas (claro/oscuro) y
  ajustar si es necesario una vez integrado en la app/Flatpak.

---

## Empaquetado y distribución

- Flatpak con runtime `org.kde.Platform` (KDE Frameworks 6 / Qt 6).
- Manifest `org.kde.<nombre>.json` (a definir nombre final).
- Repositorio en GitHub, con CI (GitHub Actions) para build de Flatpak y
  pruebas.
- Publicación futura en Flathub.

---

## Hoja de ruta

### Fase 0 — Fundamentos (en curso)
- [x] Decidir nombre definitivo de la app y app ID (Kweek / io.github.neldoreth.Kweek)
- [x] Diseñar icono moderno (SVG inicial en `icons/`)
- [x] Estructura inicial del proyecto (CMake, Kirigami app skeleton) — compila y ejecuta
- [x] Repositorio git + GitHub remoto (https://github.com/neldoreth/kweek)
- [ ] Esqueleto de manifest Flatpak

### Fase 1 — Núcleo local
- [x] Modelo de datos de eventos (KCalendarCore), CRUD y reschedule
- [x] Crear/editar/eliminar eventos (calendario local)
- [x] Recurrencias básicas (diaria/semanal/mensual/anual vía RRULE)
- [x] Acciones de posponer/adelantar (reschedule manteniendo duración)
- [x] Vista de agenda semanal (lista)
- [x] Vista de mes (cuadrícula 6x7 con chips de eventos, +N more)
- [x] Vista de semana (rejilla horaria 24h, eventos todo el día separados,
      indicador de "ahora")
- [x] Vista de día (rejilla horaria 24h, drag & drop para mover/redimensionar
      igual que la vista de semana)
- [x] Drag & drop para mover/redimensionar eventos (vista de semana: arrastrar
      bloque para cambiar día/hora, asa inferior para redimensionar duración;
      vista de mes: arrastrar chip de evento a otro día)
- [x] Colores de calendario personalizables (por calendario, con override
      opcional por evento)
- [x] Múltiples calendarios locales (panel lateral con alta/baja, color,
      renombrado y visibilidad por calendario)

### Fase 2 — Sincronización en la nube
- [x] Soporte CalDAV genérico (incl. Apple/iCloud, Nextcloud, Fastmail) —
      descubrimiento, pull y push de eventos; ver detalles arriba
- [x] Integración Google Calendar (OAuth2 + API) — verificada end-to-end:
      login, descubrimiento, sync inicial, sync incremental con `syncToken`,
      push/borrado individual de eventos y borrado de cuenta (ver
      "Verificado")
- [x] Integración Microsoft Graph (personal + 365/trabajo) — OAuth2 PKCE
      (cliente público), descubrimiento de calendarios, sync inicial e
      incremental vía delta query, push/borrado de eventos, recurrencia
      simple (daily/weekly/absoluteMonthly/absoluteYearly interval=1). Build
      limpio y smoke test offscreen ok; **pendiente verificación end-to-end
      con una cuenta Microsoft real** (requiere app registration de Azure AD
      propio, ver `microsoftoauthconfig.h.example`); excepciones de
      recurrencia (`type: "occurrence"/"exception"`) se omiten en el pull
      (limitación v1, ver sección "Microsoft Graph" arriba)
- [x] Gestión de múltiples cuentas, KWallet (cuentas CalDAV; credenciales en
      KWallet vía `CredentialStore`)
- [x] Excepciones de recurrencia remotas (`RECURRENCE-ID`/`recurringEventId`)
      ahora se sincronizan (pull) y se muestran correctamente vía
      `OccurrenceIterator`; editar instancias individuales desde Kweek sigue
      sin estar soportado (ver "Verificado")
- [x] Reintento de pushes fallidos (`pendingPush`): un push fallido ya no se
      pierde silenciosamente — se reintenta en cada sync y protege la edición
      local de ser sobrescrita por el pull mientras esté pendiente (last
      write wins sigue siendo la estrategia base; sin UI de resolución de
      conflictos)

### Fase 3 — Extras
- [ ] Widget de tiempo (Open-Meteo / AEMET, selección de ciudad)
- [ ] Detección de enlaces de videollamada
- [ ] Búsqueda de eventos
- [ ] Recordatorios múltiples y snooze
- [ ] Pulido visual final, temas claro/oscuro, animaciones

### Fase 4 — Distribución
- [ ] Empaquetado Flatpak completo y probado
- [ ] CI/CD en GitHub Actions
- [ ] Publicación en Flathub

---

## Decisiones pendientes / preguntas abiertas

- Nombre definitivo de la aplicación.
- ¿AEMET además de Open-Meteo, o solo Open-Meteo para simplificar?
- ¿Soporte de tareas/to-dos además de eventos (como BusyCal)?
- Estrategia exacta de resolución de conflictos de sincronización: v1 sigue
  siendo "last write wins" + reintento de `pendingPush` (ver "Verificado");
  pendiente decidir si hace falta una UI de conflictos para casos donde el
  reintento automático falle repetidamente.
- **Integración Google Calendar**: implementada y verificada end-to-end con
  cuenta real (ver "Estado actual" y "Verificado"). Las credenciales OAuth
  reales se guardan en `src/core/googleoauthconfig.h` (gitignored, **no se
  sube a GitHub** por ser un repo público); hay un
  `src/core/googleoauthconfig.h.example` committeado como plantilla con
  instrucciones. Sync incremental con `syncToken` (incl. fallback por token
  caducado), push/borrado de eventos individuales y borrado de cuenta ya
  verificados end-to-end. Pendiente: resolución de conflictos y excepciones
  de recurrencia (limitación v1 conocida).
- **Integración Microsoft Graph**: implementada (ver sección "Microsoft
  Graph" arriba), build limpio y smoke test offscreen ok. Las credenciales
  OAuth reales se guardan en `src/core/microsoftoauthconfig.h` (gitignored,
  **no se sube a GitHub**); hay un `src/core/microsoftoauthconfig.h.example`
  committeado como plantilla con instrucciones para crear el app registration
  de Azure AD. Pendiente verificación end-to-end con una cuenta real (login,
  descubrimiento, sync inicial/incremental, push/borrado) cuando se disponga
  de un client ID; excepciones de recurrencia (`type: "occurrence"/
  "exception"`) no se sincronizan (limitación v1).
