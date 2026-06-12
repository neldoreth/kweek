# Kalendarios — Cliente de calendario moderno para KDE

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

Proyecto recién iniciado. Sin código todavía. Este documento sirve como
plan de referencia y hoja de ruta para retomar el trabajo entre sesiones.

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

- Necesita un **icono de aplicación moderno**, siguiendo las guías de
  iconografía de KDE (Breeze) pero con personalidad propia (estilo
  BusyCal/Fantastical: ilustración limpia, gradientes suaves, posible
  referencia a fecha/página de calendario).
- Formato SVG escalable, variantes para distintos tamaños/temas
  (claro/oscuro), siguiendo el estándar `org.kde.<nombre>` para Flatpak.
- Tarea pendiente: definir nombre final de la app (afecta al app ID del
  Flatpak, ej. `org.kde.<nombre>`) y diseñar el icono.

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
- [ ] Decidir nombre definitivo de la app y app ID
- [ ] Diseñar icono moderno (SVG, claro/oscuro)
- [ ] Estructura inicial del proyecto (CMake, Kirigami app skeleton)
- [ ] Repositorio git + GitHub remoto
- [ ] Esqueleto de manifest Flatpak

### Fase 1 — Núcleo local
- [ ] Modelo de datos de eventos (KCalendarCore) + caché SQLite
- [ ] Vistas: mes, semana, día, agenda/lista
- [ ] Crear/editar/eliminar eventos (calendario local)
- [ ] Recurrencias (RRULE)
- [ ] Drag & drop para mover/redimensionar eventos
- [ ] Acciones de posponer/adelantar
- [ ] Colores de calendario personalizables

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
