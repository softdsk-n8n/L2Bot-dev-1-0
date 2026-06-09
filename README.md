# Yet another ingame bot for Lineage 2.

L2Bot — это комплекс инструментов для автоматизации Lineage 2 Interlude. Состоит из инжектируемой DLL (C++) и клиента (C# WPF), работающих через Named Pipe.

![image_2023-08-09_00-43-10](https://github.com/k0t9i/L2Bot2.0/assets/7733997/8b4356b0-f362-4ba8-8ca2-8c5cb949d873)

---

## Состав проекта

| Компонент | Язык | Описание |
|-----------|------|----------|
| **L2BotCore** | C++ | Фреймворк бота — абстракции для мира, сущностей, сообщений, сервисов. |
| **L2BotDll** | C++ | Инжектируемая DLL, перехватывающая игровые функции Lineage 2. |
| **InjectionLibrary** | C++ | Вспомогательная библиотека для инжекции кода (CreateRemoteThread, trampoline). |
| **Client** | C# WPF | Приложение-контроллер с визуальной картой на основе WPF. |
| **L2jGeodataPathFinder** | C++ | Нативная библиотека поиска пути по геодане L2J. |

---

## Как работает инжекция

### Принцип работы

Инжекция осуществляется через классический метод **DLL Injection** с использованием `CreateRemoteThread` и `LoadLibraryA`:

1. **Открытие процесса**: инжектор (`inj_v2.exe`) получает хендл L2 процесса через `OpenProcess` с правами `PROCESS_ALL_ACCESS`. Требуются права администратора (`UAC Elevate`), иначе вернётся `ERROR_ACCESS_DENIED`.
2. **Выделение памяти**: через `VirtualAllocEx` в адресном пространстве L2 выделяется блок памяти под путь к DLL.
3. **Запись пути**: `WriteProcessMemory` копирует строку с абсолютным путём к `L2BotDll.dll` в выделенную область L2.
4. **Создание потока**: `CreateRemoteThread` запускает в L2 удалённый поток, стартующий с адреса `LoadLibraryA` (из `kernel32.dll`, который одинаков в x86-процессах). Аргумент — указатель на записанный путь.
5. **Загрузка DLL**: L2 загружает `L2BotDll.dll` как собственный модуль. Срабатывает `DllMain` с флагом `DLL_PROCESS_ATTACH`.
6. **Инициализация hooks**: в `DLL_PROCESS_ATTACH` происходит:
   - Обнаружение ключевых указателей (UGameEngine, UNetworkHandler, FL2GameData)
   - Установка `JMP`/trampolines-хуков на игровые функции: `ProcessEvent`, `GetNextItem`, `Tick`, `OnDie`, `OnAttack` и др.
   - Путь: перехват → чтение/модификация игровых структур → сериализация в JSON → отправка через Named Pipe.

### Почему работает

- **L2 Interlude** (2007) — игра не имеет современной anti-cheat защиты (нет EAC, BattlEye, Vanguard).
- Все компоненты **x86**: и L2.exe, и DLL, и инжектор. Нет проблем с WOW64 (редиректором).
- Windows не блокирует операции `VirtualAllocEx`/`CreateRemoteThread` между процессами одного пользователя при наличии прав администратора.
- **Re-injection**: повторная инжекция в тот же процесс **не работает** — `DLL_PROCESS_ATTACH` вызывается только один раз за жизнь процесса.

### Обход (или его отсутствие)

Фактически **обход не требуется** — игра не защищена от базовых user-mode инжекций.
Единственное препятствие — `SeDebugPrivilege`, которое нужно получить через `UAC: Запуск от имени администратора`.

---

## Архитектура взаимодействия

```
+-------------+        Named Pipe         +-------------+
| Client (C#)| <====== PipeL2Bot ======> |L2BotDll (C+)|
|  WPF / AI   |   JSON {entities, cmds}   |   Hooks     |
+-------------+                           +------+------+
                                                |
                                           Hooked funcs
                                                |
                                           +----v------+
                                           | L2.exe    |
                                           +-----------+
```

**Поток данных:**
1. DLL перехватывает игровой тик → считывает `APlayerController`, `ANPC`, `AItem`, `UHero` из памяти L2.
2. Данные сериализуются в JSON и записываются в Named Pipe (`PipeL2Bot`).
3. C# Client читает JSON → десериализует в Entity-объекты (`Hero`, `NPC`, `Drop`, `Item`).
4. AI (Client) принимает решение → пишет команду (`MoveToLocation`, `AcquireTarget`, `RequestAttack`) в Pipe.
5. DLL получает команду → вызывает оригинальную игровую функцию через hook/trampoline.

---

## Полный функционал бота

### 1. Радар / карта
- Отображение персонажа (hero) на карте
- Отображение NPC (мобы, гварды)
- Отображение `Drop` (валяющийся лут)
- Отображение `Item` на земле
- Отображение `Player` (другие игроки)
- Клик по карте → персонаж идёт туда (pathfinding)
- Масштабирование (колёсико мыши)

### 2. Combat AI — автоматический фарм
**Состояния (State Machine):**
| Состояние | Что делает | Переход на |
|-----------|-----------|-----------|
| `Idle` | Ожидание, выбор цели | FindTarget (если есть мобы) |
| `FindTarget` | Выбор ближайшего моба по конфигу | MoveToTarget |
| `MoveToTarget` | Движение к мобу через pathfinding | Attack |
| `Attack` | Атака скиллами/автоатака | Pickup (если моб убит) |
| `Pickup` | Сбор дропа (Item/Drop) | Idle |
| `MoveToSpot` | Возврат в центр фарм-зоны | Idle (если пришёл) |
| `Rest` | Сидение (восстановление HP/MP) | Idle (HP/MP >= порога) |
| `Dead` | Мёртв, ждёт реса | Idle (после реса) |

**Возможности:**
- **Proactive targeting**: бот сам ищет мобов в зоне, даже если не был атакован
- **Zone farming**: определение радиуса/центра зоны — бот не уходит далеко
- **Mob filtering**: включение/исключение мобов по ID
- **Level filters**: ограничение по разнице уровней (нижний/верхний)
- **Skill rotation**: условия использования скиллов (HP% моба, MP% героя, HP% героя)
- **Spoil**: авто-использование Spoil на мобов
- **Soulshots**: авто-включение соулшотов
- **Autouse / autouse toggle**
- **Rest thresholds**: сидение при HP/MP ниже заданного %
- **Pickup**: сбор указанного дропа по ItemId, в радиусе
- **Pathfinding**: поиск пути через `L2jGeodataPathFinder` (нативная библиотека)

### 3. Deleveling AI — снижение уровня
- Поиск городских гвардов (Guard)
- Атака гвардов до достижения заданного уровня
- Использование указанного скилла
- Дистанция атаки настраивается

### 4. Инжекция и запуск
**Требования:**
- Windows 10/11 x64 (но L2 и DLL — x86)
- Права администратора для инжектора
- .NET 6 Runtime (для Client)

**Шаги запуска:**
1. Собрать решение MSBuild (`Release x86`) — DLL
2. Собрать Client (`dotnet publish`) — EXE
3. Скопировать `L2BotDll.dll` в `E:\L2Teon\system\`
4. Запустить L2, залогиниться
5. Инжектировать DLL от имени администратора:
   ```
   inj_v2.exe E:\L2Teon\system\L2BotDll.dll <PID>
   ```
6. Запустить `Client.exe`
7. В Client нажать **Start** (Toggle AI)

---

## Сборка

### MSBuild (DLL + C++ проекты)
```bash
"C:\Program Files (x86)\...\MSBuild.exe" L2Bot.sln /p:Configuration=Release /p:Platform="x86" /t:Rebuild
```

### .NET (Client)
```bash
cd Client
dotnet build -c Release
# или
# dotnet publish -c Release -r win-x86 --self-contained
```

### Деплой
```bash
copy Release\bin\L2BotDll.dll E:\L2Teon\system\
copy Client\publish\* E:\L2RebornBot\...
```

---

## Конфигурация

Файл `Client/config.json`:
```json
{
  "ConnectionPipeName": "PipeL2Bot",
  "MaxPassableHeight": 30,
  "GeoDataDirectory": "geodata",
  "NodeWaitingTime": 3
}
```

**AI Config** (в приложении Client):
- Combat Zone (X, Y, радиус)
- Included/Excluded Mob IDs
- Rest HP/MP thresholds
- Pickup Radius, Item IDs
- Attack Distance (mili/bow)
- Skill conditions

---

## Важно

- **Re-injection невозможен**: `DLL_PROCESS_ATTACH` срабатывает один раз. Чтобы переинжектить — нужно перезапустить L2.
- **UAC**: инжектор требует права администратора (`Run as Administrator`).
- **x86 only**: клиент, DLL и инжектор — 32-bit. На x64 Windows используется WOW64.

---

## Лицензия

Это форк/модификация проекта L2Bot2.0 (k0t9i). Используйте на свой страх и риск.
