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
  envuelve un `KCalendarCore::MemoryCalendar` persistido en
  `~/.local/share/Kweek/Kweek/calendar.ics` (vía `FileStorage`). Expone
  `addEvent`, `updateEvent`, `removeEvent` y `rescheduleEvent` (posponer/
  adelantar manteniendo duración) a QML. Cada evento soporta: resumen,
  descripción, ubicación, inicio/fin, todo el día, color (custom property
  `X-KDE-KWEEK-COLOR`), recurrencia simple (ninguna/diaria/semanal/mensual/
  anual), recordatorio (alarma Display N minutos antes) y disponibilidad
  ocupado/libre (TRANSP).
- `src/core/eventlistmodel.h/.cpp` — `EventListModel` (QAbstractListModel,
  QML_ELEMENT): expande ocurrencias (incl. recurrentes) dentro de
  `[rangeStart, rangeEnd)` usando `OccurrenceIterator`, ordenadas por inicio.
  Roles: `uid`, `summary`, `description`, `location`, `start`, `end`,
  `allDay`, `color`, `recurring`, `recurrence`, `reminderMinutes`, `busy`.
- `src/qml/CalendarPage.qml` + `src/qml/EventEditDialog.qml` — vista de
  agenda semanal mínima para probar el modelo: alta/edición/borrado y
  acciones rápidas de posponer +1h/+1día por evento.

Verificado: compila limpio y se ejecuta sin errores QML
(`cmake -B build -G Ninja && cmake --build build`); probado end-to-end
(add/reschedule/update) inspeccionando el `.ics` resultante.

Nota de CMake: fue necesario añadir `target_include_directories(kweek
PRIVATE core)` para que la generación automática de `qmltyperegistrations`
encuentre `calendarmanager.h`/`eventlistmodel.h` por nombre simple.

Dependencias de desarrollo necesarias (Arch): `cmake`, `extra-cmake-modules`,
`ninja` (o `make`), Qt6 (`qtbase`, `qtdeclarative`), `kirigami2`/`kirigami` (KF6),
`kcoreaddons`, `ki18n`, `kcalendarcore`.

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
- [x] Vista de agenda mínima de prueba (lista semanal)
- [ ] Vistas: mes, semana, día (calendario visual completo)
- [ ] Drag & drop para mover/redimensionar eventos
- [ ] Colores de calendario personalizables (por calendario, no solo por evento)
- [ ] Múltiples calendarios locales (actualmente un único `.ics`)

### Fase 2 — Sincronización en la nube
- [ ] Soporte CalDAV genérico (incl. Apple/iCloud)
- [ ] Integración Google Calendar (OAuth2 + API)
- [ ] Integración Microsoft Graph (personal + 365/trabajo)
- [ ] Gestión de múltiples cuentas, KWallet
- [ ] Sincronización bidireccional con resolución de conflictos

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
