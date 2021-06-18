Short description in English: this project defines a prototype of proxy-server plugin for QUIK terminal software, making possible to distribute financial data streams to its clients. QUIK is de-facto standard terminal to trade Russian stock/futures/etc market and adopted by most brokers. You probably have to understand Russian in order do what the QUIK is used for, so I don't see much sence in making a full translation of the project description. However, note that the code comments are mostly written in English.

# t18qsrv - прокси-сервер плагин для терминала QUIK 

Терминал QUIK производства ARQA Technologies часто используется для торговли на таких площадках, как Московская Биржа. Неоспоримыми его достоинствами являются, пожалуй, массовость поддержки брокерами и, в большинстве случае, бесплатность для конечного пользователя.

К сожалению, ARQA Technologies не предоставляет открытый интерфейс для прямого взаимодействия пользовательского кода с сервером на стороне брокера, поэтому любой код, который желает общаться с брокерским сервером, вынужден делать это посредством ~~извращений~~ работы изнутри терминала QUIK через прослойку в виде языка Lua. Далеко не во всех случаях удобно или возможно, чтобы QUIK и пользовательский код работали вместе на одной и той же машине (например, по банальным причинам не совместимости операционных систем, разрядностей, окружения и т.д. и т.п.), поэтому `t18qsrv` был разработан как способ решить эту проблему.

 `t18qsrv` является прототипом прокси-серверного плагина для QUIK, который позволяет пересылать на другие машины-клиенты котировочные данные (такие, как ohlcv изменения курса, поток обезличенных сделок и т.д.) ~~и принимать от них для исполнения торговые приказы~~ (ещё не реализовано). Потенциальный спектр возможностей принятого подхода начинается с любых действий, доступных внутри QUIK для lua-плагина, и заканчивается на любых действиях, доступных бинарному коду в принципе.

Для взаимодействия с QUIK `t18qsrv` использует проект [@elelel](https://github.com/elelel/) [QluaCpp](https://github.com/elelel/qluacpp), который исключительно упрощает жизнь пользователям языка С++. Для знакомства с `QluaCpp` см. авторский [туториал](https://github.com/elelel/qluacpp-tutorial). `luacpp` + `qluacpp` были форкнуты в [qluacpp2](https://github.com/Arech/qluacpp2) и обновлены для использования в `t18qsrv`. Подробнее по ссылке репы `qluacpp2`

## Почему t18qsrv это пока что "прототип"

Во первых, на данный момент из существенных возможностей реализована только раздача клиентам потока обезличенных сделок. Этого достаточно для передачи данных в другие терминальные программы, что реализовано в проекте [Q2Ami](https://github.com/Arech/Q2Ami),
 однако для роботрейдинга, например, маловато в силу отсутствия средств работы с торговыми позициями.

Во вторых, протокол передачи данных наивный насколько, насколько он вообще может быть наивным - для передачи данные никак не сериализуются, а просто прямо копируются из памяти в сеть, оснащаясь лишь коротким заголовком для различения пакетов. Это позволяет работать быстро и, в принципе, работает на сходных архитектурах и одинаковых компиляторах и их настройках, но это, всё же, нельзя назвать полноценным протоколом.

В третьих, передача данных осуществляется без шифрования, что позволяет безопасно использовать `t18qsrv` и его клиентов только внутри одной сети, защищённой из-вне. В принципе, это, должно быть, наименьшая проблема, т.к. при необходимости, шифрование должно быть не сложно подключить [штатными средствами boost::asio](https://www.boost.org/doc/libs/1_70_0/doc/html/boost_asio/overview/ssl.html).

## Статус кода

Всё стабильно работает как задумано, ~~проблем неизвестно~~. Код работы с сетью брался из официального примера boost::asio, а тот оказался забагован утечкой памяти, что я не поймал, когда разбирался. Подробно об этой баге [тут](https://habr.com/ru/post/471326/). Утечка не значительная по объёму на одно соединение, поэтому опасность определяйте исходя из собственных сценариев использования. Если почините раньше меня - сделайте пулл, плс.

Для успешной работы с `t18qsrv` нужно понимание особенностей работы подсистемы `lua` внутри QUIK. В частности, важно понимать, что для успешного получения в `lua`, а значит и в `t18qsrv`, потока обезличенных сделок по тикеру, необходимо сначала настроить в QUIK их приём от брокера (например, создать таблицу обезличенных сделок по нужному тикеру и удостовериться в получении потока данных; более того, как минимум в QUIK v.8.13.0.106 хотя бы одна таблица обезличенных сделок должна быть открыта даже если заказ обезличенных сделок указан в настройках). Заказать получение сделок через API в `lua` (на момент написания) невозможно, поэтому запрос потока сделок, который не был предварительно заказан вручную, вернёт просто пустое множество, по которому невозможно понять, причина пустоты в реальном ли отсутствии сделок или в отсутствии заказа потока.

### Пример использования

См. проект [Q2Ami](https://github.com/Arech/Q2Ami) - data-source плагин для терминала AmiBroker, который в реальном времени с помощью `t18qsrv` получает и отображает поток обезличенных сделок из QUIK. В отличие от плагина для AmiBroker, идущего в составе поставки QUIK (или свободно скачиваемого с оф.сайта), `Q2Ami` совместно с `t18qsrv` позволяет держать QUIK на одной машине и работать с данными из него внутри AmiBroker на другой.

### Условия использования

Программа поставляется на условиях стандартной лицензии свободного ПО GNU GPLv3. Если вы хотите использовать плагин или его часть так, как лицензия прямо не разрешает, пишите на `aradvert@gmail.com`, договоримся.

Если вы зарабатываете с участием плагина деньги, то, наверное, должны понимать сколько труда стоит создать и поддерживать такую работу. Сделайте донат для покрытия расходов на разработку на своё усмотрение. За деталями так же пишите на `aradvert@gmail.com`.

### Поддерживаемые версии QUIK и версии Lua

64 битные QIUK с версий 8.5 на Lua 5.3. Точно работает на QUIK 8.8.1.5. Детали настройки QluaCpp и необходимые файлы Lua SDK описаны в [родном туториале](https://github.com/elelel/qluacpp-tutorial/tree/master/basic_tutorial).

### Запуск внутри QUIK

Собранный `t18qsrv.dll` и файл `t18proxysrv.lua` необходимо скопировать в корневой каталог установки QUIK. Далее запустить `t18proxysrv.lua` на исполнение внутри терминала как обыкновенный lua-скрипт. Сервер запустится и будет ожидать подключений. Моменты подключений и подписки клиентов на потоки обезличенных сделок отображаются с помощью стандартных информационных сообщений.

### Поддерживаемый компилятор

`t18qsrv` разработан с использованием превосходного компилятора [Clang](https://clang.llvm.org/) v6 набора LLVM, поэтому версия 6 или, скорее всего, более новые версии Сlang будут работать сразу*. Другие современные компиляторы, полноценно поддерживающие спецификацию С++17 теоретически должны работать тоже, но, возможно, придётся внести небольшие правки.

Среда Visual Studio 2015 (проект которой присутствует в исходных кодах) используется только как редактор кода и инструмент управления компиляцией. Использование её не обязательно. Для компилятора VC версии 2015 проект, скорее всего, будет не по зубам, из-за зависимостей на фреймфорк [t18](https://github.com/Arech/t18), требующий поддержки С++17, но, возможно, более свежие VC2017 или VC2019 уже справятся.

Обратите внимание, что в проекте определён `Post-Build Event` в котором собранная `.dll` проекта и подключающий её lua файл `t18proxysrv.lua` копируются в отдельную папку для удобства дальнейшего копирования в экземпляр установки QUIK. Вам нужно перенастроить под себя или убрать этот шаг.

### Внешние зависимости

- Кроме стандартных компонентов STL (использовалась версия stl из VC2015, но с другими не более старыми версиями проблем быть не должно)  используются только некоторые компоненты библиотеки [Boost](https://www.boost.org/) в режиме "только заголовочные файлы" (использовалась версия 1.75, более свежие должны работать из коробки). Компиляции Boost не требуется.

- Фреймворк [t18](https://github.com/Arech/t18) содержит необходимые описания протокола и некоторые иные нужные части. Фреймворк достаточно скачать/клонировать в __над__-каталог проекта, в папку `../t18`, и он подхватится автоматически. На всякий случай: каждый новый коммит этого проекта полагается на самый последний коммит `t18`.

- [QluaCpp](https://github.com/elelel/qluacpp) используется в качестве интерфейса к QUIK. Вам нужно выкачать форкнутый репозиторий [qluacpp2](https://github.com/Arech/qluacpp2) и разложить его внутрь папки  `../_extern/qluacpp2`, либо в любое другое место, исправив пути к их папкам `/include` на листе `VC++ Directories` свойства проекта, в атрибуте `Include Directories`.

  - так же необходимо скачать и разахивировать с любое место  бинарники Lua c оф.страницы проекта [https://sourceforge.net/projects/luabinaries/files/](https://sourceforge.net/projects/luabinaries/files/) той версии, которая максимально близка к используемой в QUIK. Сейчас это Lua 5.3. Ей соответствует самая свежая версия [5.3.5](https://sourceforge.net/projects/luabinaries/files/5.3.5/Windows%20Libraries/Dynamic/) с сайта проекта. Для линковки из под VC2015 нужен архив [lua-5.3.5_Win64_dll14_lib.zip](https://sourceforge.net/projects/luabinaries/files/5.3.5/Windows%20Libraries/Dynamic/lua-5.3.5_Win64_dll14_lib.zip/download). После скачивания и разархивирования, необходимо в свойствах проекта для соответствующей архитектуры сделать правки на том же листе `VC++ Directories`, - добавить путь к `/include` в атрибут `Include Directories` и добавить путь к корню разархивированной папки в атрибут `Library Directories`. (возможно так же придётся переименовать папку из `lua-5.3.5_Win64_dll14_lib` в `lua-5.3.5_x64_dll14_lib` для использования макроса `$(Platform)`, либо просто убрать его на целое имя папки).

  -  для решения проблем со сборкой проекта из-за `QLuaCpp`, смотрите [туториал](https://github.com/elelel/qluacpp-tutorial/tree/master/basic_tutorial).

- Быстрая lock-free очередь [readerwriterqueue](https://github.com/cameron314/readerwriterqueue). Проект надо скачать/клонировать в __над__-каталог `../_extern/readerwriterqueue/` и всё подхватится автоматически.

## Change Log

### 2021 Apr 01

- изменилась семантика параметра последнего времени сделок для запроса `SubscribeAllTrades`. То, что раньше было `lastKnownDeal`, время последней известной клиенту сделки (вызывало возвращение всех сделок строго после этого момента), теперь стало `dealsSinceTs` - время самой ранней сделки, нужной клиенту (вызывает возвращение всех сделок начиная с этого момента включительно).

### 2021 Mar 25

- Апдейт на свежий буст 1.75
- Апдейт на Lua 5.3 и QUIK от 8.5 с 64битными идентификаторами сделок. Старые версии не поддерживаются.
- исправления к `luacpp` и `qluacpp` выложены в [qluacpp2](https://github.com/Arech/qluacpp2), `qluacpp2` теперь должен лежать в `../_extern/qluacpp2/`