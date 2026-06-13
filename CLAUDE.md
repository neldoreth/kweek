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
    `syncStarted`/`syncFinished`/`syncError(calendarId, error)`. **Limitación
    v1**: las excepciones de recurrencia (`RECURRENCE-ID`) del servidor se
    ignoran al sincronizar.
  - Push-on-edit: `addEvent`/`updateEvent`/`rescheduleEvent`/
    `moveEventToCalendar` llaman a `pushEvent` (serializa con `ICalFormat` y
    hace PUT, actualizando `syncItems`) y `removeEvent`/`moveEventToCalendar`
    (en el calendario origen) llaman a `pushDelete` (DELETE remoto) para
    calendarios tipo "caldav". Sincronización v1 = "last write wins", sin UI
    de resolución de conflictos.
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
    resync completo. **Limitación v1** (igual que CalDAV): instancias de
    excepción (`recurringEventId`) se ignoran.
  - `pushEvent`/`pushDelete` ganan rama `type == "google"`: al crear un
    evento, Google asigna su propio id y el UID local se renombra para que
    coincida (clonando el evento con el nuevo UID).
  - `src/qml/AddGoogleAccountDialog.qml` — diálogo con botón "Sign in with
    Google" + estado/progreso (señal `googleAccountAdded`).
    `CalendarSidebar.qml` añade botón "Connect Google account…" y extiende
    el botón/indicador de sync manual a calendarios `type: "google"`.
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

Pendiente: probar pull/push/borrado de eventos individuales y sync
incremental (segunda sincronización con `syncToken`) con esta cuenta, y
borrado de cuenta.

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
- [x] Integración Google Calendar (OAuth2 + API) — verificada end-to-end con
      cuenta real (login, descubrimiento y sync inicial); pendiente probar
      sync incremental y push/borrado individual (ver "Verificado")
- [ ] Integración Microsoft Graph (personal + 365/trabajo)
- [x] Gestión de múltiples cuentas, KWallet (cuentas CalDAV; credenciales en
      KWallet vía `CredentialStore`)
- [ ] Sincronización bidireccional con resolución de conflictos (v1 = last
      write wins; excepciones de recurrencia remotas no soportadas aún)

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
- Estrategia exacta de resolución de conflictos de sincronización.
- **Integración Google Calendar**: implementada y verificada end-to-end con
  cuenta real (ver "Estado actual" y "Verificado"). Las credenciales OAuth
  reales se guardan en `src/core/googleoauthconfig.h` (gitignored, **no se
  sube a GitHub** por ser un repo público); hay un
  `src/core/googleoauthconfig.h.example` committeado como plantilla con
  instrucciones. Pendiente: sync incremental (segunda pasada con
  `syncToken`), push/borrado de eventos individuales, borrado de cuenta y
  resolución de conflictos.
