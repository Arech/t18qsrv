Short description in English: this project makes a prototype of proxy-server plugin for QUIK terminal software, making possible distribution of financial data to its clients as well as executing trading orders within QUIK. QUIK terminal is commonly used to trade Russian stock/futures/etc market. You probably have to understand Russian in order do what the QUIK is used for, so I don't see much sence in making a full translation of the project description. However, note that the code comments are mostly written in English.

# t18qsrv - прокси-сервер плагин для терминала QUIK 

Терминал QUIK производства ARQA Technologies часто используется для торговли на таких площадках, как Московская Биржа. Неоспоримыми его достоинствами являются, пожалуй, массовость поддержки брокерами и, в большинстве случае, бесплатность для конечного пользователя.

К сожалению, ARQA Technologies не предоставляет открытый интерфейс для прямого взаимодействия пользовательского кода с сервером на стороне брокера, поэтому любой код, который желает общаться с брокерским сервером, вынужден делать это посредством ~~извращений~~ работы изнутри терминала QUIK. Далеко не во всех случаях удобно или возможно, чтобы QUIK и пользовательский код работали вместе на одной и той же машине (например, не совместимость операционных систем, разрядностей, окружения и т.д. и т.п.), поэтому `t18qsrv` был разработан как способ решить эту проблему.

 `t18qsrv` является прототипом прокси-серверного плагина для QUIK, который позволяет пересылать на другие машины-клиенты котировочные данные (такие, как ohlcv изменения курса, поток обезличенных сделок и т.д.) и принимать от них для исполнения торговые приказы. Потенциальный спектр возможностей начинается с любых действий, доступных внутри QUIK для lua-плагина, и заканчивается на любых действиях, доступных бинарному коду в принципе.

Для взаимодействия с QUIK `t18qsrv` использует замечательный проект @elelel [QluaCpp](https://github.com/elelel/qluacpp), который исключительно упрощает жизнь пользователям языка С++. Для знакомства с `QluaCpp` см. авторский [туториал](https://github.com/elelel/qluacpp-tutorial).

## Почему t18qsrv это пока что "прототип"?

Во первых, на данный момент из существенных возможностей реализована только раздача клиентам потока обезличенных сделок. Этого достаточно для передачи данных в другие терминальные программы, что реализовано в проекте `Q2Ami` (он будет опубликован в ближайшее время),
 однако для роботрейдинга, например, маловато в силу отсутствия средств работы с торговыми позициями.

Во вторых, протокол передачи данных наивный насколько, насколько он вообще может быть наивным - для передачи данные никак не сериализуются, а просто прямо копируются из памяти в сеть, оснащаясь лишь коротким заголовком для различения пакетов. Это позволяет работать быстро и, в принципе, работает на сходных архитектурах и одинаковых компиляторах и их настройках, но это, всё же, сложно назвать полноценным протоколом.

В третьих, передача данных осуществляется без шифрования, что позволяет безопасно использовать `t18qsrv` и его клиентов только внутри одной сети, защищённой из-вне. В принципе, это, должно быть, наименьшая проблема, т.к. при необходимости, шифрование должно быть не сложно подключить [штатными средствами boost::asio](https://www.boost.org/doc/libs/1_70_0/doc/html/boost_asio/overview/ssl.html).

## Статус кода

Всё стабильно работает как задумано, проблем неизвестно.

### Пример использования

См. проект `Q2Ami` (будет опубликован в ближайшее время) - data-source плагин для терминала AmiBroker, который в реальном времени с помощью `t18qsrv` получает и отображает поток обезличенных сделок из QUIK. В отличие от плагина для AmiBroker, идущего в составе поставки QUIK (или свободно скачиваемого с оф.сайта), `Q2Ami` совместно с `t18qsrv` позволяет держать QUIK на одной машине и работать с данными из него внутри AmiBroker на другой.

### Условия использования

Программа поставляется на условиях стандартной лицензии свободного ПО GNU GPLv3. Если вы хотите использовать плагин или его часть так, как лицензия прямо не разрешает, пишите на `aradvert@gmail.com`, договоримся.

Если вы зарабатываете с участием плагина деньги, то, наверняка, должны понимать сколько труда стоит создать такую работу. Сделайте донат для покрытия расходов на разработку на своё усмотрение. За деталями так же пишите на `aradvert@gmail.com`.

### Поддерживаемые версии QUIK

Должен работать на всех относительно современных версиях терминала QUIK, где успешно работает [QluaCpp](https://github.com/elelel/qluacpp). Точно работает на последних версиях QUIK ветки 7 (32х разрядных), и ветки 8 (64х разрядных).

### Запуск внутри QUIK

Собранный `t18qsrv.dll` и файл `t18proxysrv.lua` необходимо скопировать в корневой каталог установки QUIK. Далее запустить `t18proxysrv.lua` на исполнение внутри терминала как обыкновенный lua-скрипт. Сервер запустится и будет ожидать подключений. Моменты подключений и подписки клиентов на потоки обезличенных сделок отображаются с помощью стандартных информационных сообщений.

### Поддерживаемый компилятор

`t18qsrv` разработан с использованием превосходного компилятора [Clang](https://clang.llvm.org/) v6 набора LLVM, поэтому версия 6 или более новые версии Сlang будут работать сразу. Другие современные компиляторы, полноценно поддерживающие спецификацию С++17 теоретически должны работать тоже, но, возможно, придётся внести небольшие правки.

Среда Visual Studio 2015 (проект которой присутствует в исходных кодах) используется только как редактор кода и инструмент управления компиляцией. Использование её не обязательно. Для компилятора VC версии 2015 проект, скорее всего, будет не по зубам, из-за зависимостей на фреймфорк [t18](https://github.com/Arech/t18), требующий поддержки С++17, но, возможно, более свежие VC2017 или VC2019 уже справятся.

Обратите внимание, что в проекте определён `Post-Build Event` в котором собранная `.dll` проекта и подключающий её lua файл `t18proxysrv.lua` копируются в отдельную папку для удобства дальнейшего копирования в экземпляр установки QUIK. Вам нужно перенастроить под себя или убрать этот шаг.

### Внешние зависимости

- Кроме стандартных компонентов STL (использовалась версия stl из VC2015, но с другими не более старыми версиями проблем быть не должно)
 используются только некоторые компоненты библиотеки [Boost](https://www.boost.org/) в режиме "только заголовочные файлы" (использовалась версия 1.70, более свежие должны работать из коробки). Компиляции Boost не требуется.

- Фреймворк [t18](https://github.com/Arech/t18) содержит необходимые описания протокола и некоторые иные нужные части. Фреймворк достаточно скачать/клонировать в __над__-каталог проекта, в папку `../t18`, и он подхватится автоматически.

- [QluaCpp](https://github.com/elelel/qluacpp) используется в качестве интерфейса к QUIK. Вам нужно скачать исходный код проектов `QluaCpp` и [luacpp](https://github.com/elelel/luacpp), и прописать в свойствах проекта пути к их папкам `/include` на листе `VC++ Directories`, в атрибуте `Include Directories`.

  - так же необходимо скачать и разахивировать с любое место  бинарники Lua c оф.страницы проекта [https://sourceforge.net/projects/luabinaries/files/](https://sourceforge.net/projects/luabinaries/files/) той версии, которая максимально близка к используемой в QUIK. В ветках 7 и 8 используется Lua 5.1. Ей соответствует самая свежая версия [5.1.5](https://sourceforge.net/projects/luabinaries/files/5.1.5/Windows%20Libraries/Dynamic/) с сайта проекта. Для линковки из под VC2015 нужен архив [lua-5.1.5_Win32_dll14_lib.zip](https://sourceforge.net/projects/luabinaries/files/5.1.5/Windows%20Libraries/Dynamic/lua-5.1.5_Win32_dll14_lib.zip/download) для 32битной версии QUIK или [lua-5.1.5_Win64_dll14_lib.zip](https://sourceforge.net/projects/luabinaries/files/5.1.5/Windows%20Libraries/Dynamic/lua-5.1.5_Win64_dll14_lib.zip/download) для 64х битной. После скачивания и разархивирования, необходимо в свойствах проекта для соответствующей архитектуры сделать правки на том же листе `VC++ Directories`, - добавить путь к `/include` в атрибут `Include Directories` и добавить путь к корню разархивированной папки в атрибут `Library Directories`.

  -  для решения проблем со сборкой проекта из-за `QLuaCpp`, смотрите [туториал](https://github.com/elelel/qluacpp-tutorial/tree/master/basic_tutorial).

- Быстрая lock-free очередь [readerwriterqueue](https://github.com/cameron314/readerwriterqueue). Проект надо скачать/клонировать в __над__-каталог `../_extern/readerwriterqueue/` и всё подхватится автоматически.

