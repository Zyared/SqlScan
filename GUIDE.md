# Учебный разбор проекта SqlScan
> Для тех, кто не знает C++. Здесь объясняется каждое ключевое слово и конструкция, которая встречается в коде.

---

## Содержание
1. [Что делает проект](#1-что-делает-проект)
2. [Структура файлов](#2-структура-файлов)
3. [Базовые понятия C++](#3-базовые-понятия-c)
4. [Типы данных](#4-типы-данных)
5. [Директивы препроцессора](#5-директивы-препроцессора)
6. [Ключевые слова языка](#6-ключевые-слова-языка)
7. [Операторы](#7-операторы)
8. [Структуры и классы](#8-структуры-и-классы)
9. [Стандартная библиотека C++](#9-стандартная-библиотека-c)
10. [MFC и Windows API](#10-mfc-и-windows-api)
11. [Разбор файлов построчно](#11-разбор-файлов-построчно)

---

## 1. Что делает проект

**SqlScan** — это программа с графическим окном (Windows), которая принимает текст и проверяет, похож ли он на попытку SQL-инъекции.

**SQL-инъекция** — атака на базы данных. Злоумышленник вставляет в поле ввода куски SQL-кода вместо обычного текста. Например, вместо имени пользователя пишет:
```
' OR 1=1 --
```
Программа ищет подобные опасные фрагменты и ставит «очки опасности» (вес). Если очков достаточно — сигнализирует.

---

## 2. Структура файлов

```
SqlScan/
├── SqlInjectionDetector.h   — объявление «движка» детекции (что существует)
├── SqlInjectionDetector.cpp — реализация движка (как работает)
├── SqlScanDlg.h             — объявление главного окна программы
├── SqlScanDlg.cpp           — реализация окна (кнопки, отчёт, файлы)
├── SqlScan.h                — объявление самого приложения
├── SqlScan.cpp              — точка входа: запуск приложения
├── pch.h / pch.cpp          — предкомпилированный заголовок (ускорение сборки)
├── framework.h              — подключение MFC/Windows
└── resource.h               — числовые ID для кнопок, полей, диалога
```

**Правило:** файлы `.h` — это «оглавление» (объявления). Файлы `.cpp` — это «текст книги» (реализация). Сначала всегда читают `.h`, чтобы понять, что есть в классе.

---

## 3. Базовые понятия C++

### Переменная
Именованная ячейка памяти, которая хранит значение.
```cpp
int score = 5;      // целое число со значением 5
bool found = false; // логическое значение
```

### Функция
Именованный блок кода, который можно вызывать.
```cpp
int Add(int a, int b) {   // принимает два числа
    return a + b;          // возвращает их сумму
}
int result = Add(3, 4);   // result = 7
```

### Метод
Функция внутри класса. Вызывается через точку (`.`) или стрелку (`->`).

### Объект
Экземпляр класса. Как «конкретная машина» от «чертежа» (класса).

---

## 4. Типы данных

| Тип | Что хранит | Пример |
|-----|-----------|--------|
| `int` | Целое число (32 бита) | `int x = 42;` |
| `bool` | Истина/ложь | `bool ok = true;` |
| `char` | Один символ (1 байт) | `char c = 'A';` |
| `float` | Число с плавающей точкой (32 бита) | `float t = 0.5f;` |
| `BYTE` | Беззнаковый байт 0–255 (Windows) | `BYTE b = 255;` |
| `DWORD` | Беззнаковое 32-битное целое (Windows) | `DWORD d = 1024;` |
| `ULONGLONG` | Беззнаковое 64-битное целое (Windows) | размер файла |
| `UINT` | Беззнаковый int | |
| `BOOL` | Windows-версия bool: TRUE/FALSE | возврат из WinAPI |

### std::size_t
Специальный тип для размеров и индексов. Всегда беззнаковый. Гарантированно вмещает размер любого объекта в памяти. Используется, когда нельзя получить отрицательный индекс.

### COLORREF
Целое 32-битное число, закодированное как `0x00BBGGRR`. Используется для цвета в WinAPI.

---

## 5. Директивы препроцессора

Начинаются с `#`. Обрабатываются до компиляции — это как «поиск и замена» в тексте файла.

### `#pragma once`
```cpp
#pragma once
```
Говорит компилятору: «подключи этот файл только один раз, даже если его включают несколько раз». Без этого одни и те же объявления повторялись бы и вызывали ошибки.

### `#ifndef / #define / #endif`
Старый способ сделать то же самое, что `#pragma once`:
```cpp
#ifndef PCH_H   // «если PCH_H ещё не определён»
#define PCH_H   // определяем его
// ... содержимое файла ...
#endif          // конец условия
```

### `#include`
```cpp
#include <vector>         // из стандартной библиотеки (угловые скобки)
#include "SqlScan.h"      // из нашего проекта (кавычки)
```
Буквально вставляет содержимое другого файла в это место.

### `#ifdef / #endif`
```cpp
#ifdef _DEBUG
#define new DEBUG_NEW   // только в отладочной сборке
#endif
```
Код между `#ifdef` и `#endif` компилируется только если указанный символ определён.

### `#define`
```cpp
#define new DEBUG_NEW
```
Простая замена: каждое слово `new` в коде заменяется на `DEBUG_NEW`. Это «макрос».

---

## 6. Ключевые слова языка

### `namespace`
Пространство имён — «папка» для имён, чтобы не было конфликтов.
```cpp
namespace {
    // Анонимный namespace: всё внутри видно ТОЛЬКО в этом .cpp файле
    bool IsSpaceChar(char c) { ... }
}
```
`std::` — это тоже namespace: `std::string`, `std::vector`.

### `struct`
Простая структура данных: несколько полей, объединённых вместе. По умолчанию все поля публичные.
```cpp
struct DetectionHit {
    std::string pattern;   // поле: строка
    std::size_t position;  // поле: позиция
    int weight;            // поле: вес
    bool highRisk;         // поле: флаг
};
```
Создать объект: `DetectionHit h;` Обратиться к полю: `h.weight = 3;`

### `class`
Как `struct`, но по умолчанию всё **приватное** (скрыто снаружи). Используется для инкапсуляции.

### `public` / `private` / `protected`
- `public` — доступно всем
- `private` — доступно только внутри самого класса
- `protected` — доступно классу и его наследникам

### `const`
«Не изменять». Бывает в разных позициях:
```cpp
const int x = 5;           // переменная x не изменится
void Foo() const;           // метод не меняет поля объекта
const PatternRule& rule     // ссылка на неизменяемый объект
```

### `constexpr`
Константа, вычисляемая **во время компиляции** (а не во время выполнения). Быстрее и безопаснее.
```cpp
constexpr int kMax = 512 * 1024;  // компилятор вычислит 524288 сам
```

### `inline`
В контексте переменных (`inline constexpr`): позволяет определить константу прямо в `.h` файле без ошибок «множественного определения» при включении в несколько `.cpp`.

### `static`
Зависит от контекста:
- В классе: метод/поле принадлежит классу, а не конкретному объекту. Вызов: `ClassName::Method()`.
- Внутри функции: переменная существует между вызовами (живёт всю программу).
- Внутри файла (вне класса): видна только в этом файле.

### `override`
Говорит: «этот метод переопределяет метод базового класса». Компилятор проверит, что такой метод действительно есть в базовом классе.
```cpp
BOOL OnInitDialog() override;  // переопределяем метод из CDialogEx
```

### `explicit`
Запрещает неявное преобразование типов при вызове конструктора.
```cpp
explicit CSqlScanDlg(CWnd* pParent = nullptr);
// Теперь нельзя написать: CSqlScanDlg d = somePointer; (только явно)
```

### `noexcept`
Гарантия: эта функция **никогда не бросает исключений**.
```cpp
CSqlScanApp() noexcept;
```

### `return`
Завершает функцию и возвращает значение.
```cpp
return report;   // вернуть объект DetectionReport
return TRUE;     // вернуть булево значение
return;          // выйти из void-функции без значения
```

### `for`
Цикл. Три вида:

**Классический:**
```cpp
for (int i = 0; i <= last; ++i) {
    // i идёт от 0 до last включительно
}
```

**Range-for (для каждого элемента коллекции):**
```cpp
for (const PatternRule& rule : rules_) {
    // rule — ссылка на текущий элемент вектора rules_
}
```

### `if / else`
```cpp
if (chunk.size() > kMaxAnalyzeInputBytes) {
    // если условие истинно
} else {
    // иначе
}
```

### `continue`
Пропустить оставшуюся часть тела цикла и перейти к следующей итерации.

### `break`
Немедленно выйти из цикла.

### `nullptr`
Нулевой указатель — «указатель в никуда». Означает «нет объекта».

### `true` / `false`
Булевы константы: истина и ложь.

---

## 7. Операторы

### Арифметические
| Оператор | Смысл | Пример |
|----------|-------|--------|
| `+` | сложение | `a + b` |
| `-` | вычитание | `a - b` |
| `*` | умножение | `a * b` |
| `/` | деление | `a / b` |
| `%` | остаток от деления | `a % b` |
| `++i` | прибавить 1 (до использования) | `++i` |

### Сравнения
| Оператор | Смысл |
|----------|-------|
| `==` | равно |
| `!=` | не равно |
| `>` | больше |
| `<` | меньше |
| `>=` | больше или равно |
| `<=` | меньше или равно |

### Логические
| Оператор | Смысл | Пример |
|----------|-------|--------|
| `&&` | И (оба должны быть true) | `a && b` |
| `\|\|` | ИЛИ (хотя бы одно true) | `a \|\| b` |
| `!` | НЕ (инверсия) | `!found` |

**Короткое замыкание:** В `A && B`, если A ложно — B не вычисляется. В `A || B`, если A истинно — B не вычисляется.

### Тернарный оператор `?:`
```cpp
r.suspicious ? _T("ДА") : _T("НЕТ")
// если r.suspicious истинно — "ДА", иначе "НЕТ"
// короткая замена if/else для значений
```

### Оператор присваивания `=`
```cpp
report.totalScore = 0;  // записать 0 в поле totalScore
```

### Составное присваивание `+=`
```cpp
report.totalScore += rule.weight;  // то же, что: totalScore = totalScore + weight
```

### Оператор доступа к члену `.` и `->`
```cpp
report.suspicious     // доступ к полю объекта (объект, не указатель)
lpMMI->ptMinTrackSize // доступ к полю через указатель (эквивалентно (*lpMMI).ptMinTrackSize)
```

### Побитовое ИЛИ `|`
```cpp
CFile::modeRead | CFile::typeBinary
// складывает флаги в одно число (не логическое ИЛИ!)
```

### Оператор взятия адреса `&`
```cpp
m_pMainWnd = &dlg;  // взять адрес объекта dlg
```

### Оператор разыменования `*`
```cpp
*p  // получить значение по указателю p
```

---

## 8. Структуры и классы

### Конструктор
Специальный метод с тем же именем, что и класс. Вызывается при создании объекта.
```cpp
SqlInjectionDetector::SqlInjectionDetector() {
    rules_ = { ... };  // заполняем список правил
}
```

### Список инициализации (`:`)
```cpp
CSqlScanDlg::CSqlScanDlg(CWnd* pParent)
    : CDialogEx(IDD_SQLSCAN_DIALOG, pParent) {}
//   ^^^^^^^^^^ вызов конструктора базового класса
```

### Наследование (`public BaseClass`)
```cpp
class CSqlScanDlg : public CDialogEx { ... };
// CSqlScanDlg наследует всё из CDialogEx
// CSqlScanDlg — дочерний класс (derived)
// CDialogEx   — родительский класс (base)
```

### Ссылка `&` в параметрах
```cpp
void FillReportRich(const DetectionReport& r);
// r — ссылка на DetectionReport: не копируется, работаем с оригиналом
// const — обещаем не изменять r
```

### Указатель `*`
```cpp
CWnd* pParent  // указатель на CWnd: хранит адрес объекта в памяти
```
Разница ссылка/указатель: указатель может быть `nullptr`, ссылка — нет.

### `= default`
```cpp
CSqlScanApp::CSqlScanApp() noexcept = default;
// Попросить компилятор сгенерировать тело автоматически (пустое)
```

### Поле с подчёркиванием в конце
```cpp
std::vector<PatternRule> rules_;
bool layoutReady_ = false;
```
Это соглашение об именовании: `_` в конце означает «поле класса» (чтобы не путать с локальными переменными).

---

## 9. Стандартная библиотека C++

### `std::string`
Строка символов, которая умеет изменять длину.
```cpp
std::string s = "hello";
s += " world";          // конкатенация
s.size();               // длина в байтах
s.empty();              // true если пустая
s.push_back('!');       // добавить символ в конец
s.front();              // первый символ
s.back();               // последний символ
s.erase(s.begin());     // удалить первый символ
s.pop_back();           // удалить последний символ
s.reserve(1000);        // зарезервировать память (ускоряет push_back)
s.substr(0, 10);        // подстрока с позиции 0, длиной 10
s.data();               // указатель на байты строки
s.c_str();              // указатель на байты (с '\0' в конце)
```

### `std::string_view`
«Вид» на чужую строку: хранит указатель и длину, **не копирует данные**. Быстрее, чем `std::string`, когда не нужно менять данные.
```cpp
std::string_view sv = someString;
sv.size();
sv.substr(0, k);  // тоже string_view, не копирует
sv.data();        // указатель на байты оригинала
sv.empty();
```

### `std::vector<T>`
Динамический массив. `T` — тип элементов.
```cpp
std::vector<int> v;
v.push_back(42);      // добавить в конец
v.size();             // количество элементов
v.empty();            // true если пусто
v.begin(), v.end();   // итераторы (для sort и for)
v.back();             // последний элемент
v.back().second;      // если элемент — pair, получить второй
v.emplace_back(a, b); // добавить, сконструировав на месте (эффективнее push_back для сложных типов)
```

### `std::pair<A, B>`
Пара из двух значений разных типов.
```cpp
std::pair<size_t, size_t> seg = {10, 20};
seg.first;   // 10
seg.second;  // 20
```

### `std::sort`
Сортировка вектора.
```cpp
std::sort(segs.begin(), segs.end());  // по умолчанию по первому полю pair
```

### `std::min` / `std::max`
```cpp
(std::min)(a, b)  // меньшее из двух
(std::max)(a, b)  // большее из двух
// Скобки вокруг std::min нужны из-за макросов Windows: min(...) и max(...)
// без скобок Windows.h переопределяет min/max и код сломается
```

### `memcmp`
Функция из C: сравнивает `n` байт двух участков памяти.
```cpp
memcmp(hay + i, ndl, plen) == 0
// возвращает 0 если plen байт начиная с (hay+i) равны байтам ndl
```

### `static_cast<T>(expr)`
Явное преобразование типа. Безопаснее C-style-каста `(T)expr`.
```cpp
static_cast<char>(c - 'A' + 'a')   // результат арифметики → char
static_cast<WPARAM>(kMaxInputEditTchars)  // int → WPARAM
static_cast<int>(s.size())          // size_t → int
```

### `reinterpret_cast<T>(ptr)`
Переинтерпретировать указатель как другой тип (побайтово). Используется при работе с Windows API, которое работает с `char*`.
```cpp
reinterpret_cast<LPCCH>(buf.data() + off)  // BYTE* → char*
```

---

## 10. MFC и Windows API

**MFC (Microsoft Foundation Classes)** — библиотека C++ классов, которая оборачивает Windows API в удобные объекты.

### Классы MFC

| Класс | Что это |
|-------|---------|
| `CWinApp` | Само приложение. Точка входа, цикл сообщений. |
| `CDialogEx` | Диалоговое окно с расширенными возможностями (фон, DPI). |
| `CWnd` | Базовый класс любого окна/контрола. |
| `CRichEditCtrl` | Текстовое поле с форматированием (цвет, шрифт). |
| `CFileDialog` | Стандартный диалог «Открыть»/«Сохранить». |
| `CFile` | Работа с файлами. |
| `CRect` | Прямоугольник (left, top, right, bottom). |
| `CString` | Строка Windows (UTF-16 в Unicode-сборке). |
| `CStringW` | Явно широкая строка (wchar_t). |
| `CStringA` | Явно узкая строка (char). |

### Макросы MFC

```cpp
BEGIN_MESSAGE_MAP(CSqlScanDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_CHECK, &CSqlScanDlg::OnBnClickedCheck)
    ON_WM_SIZE()
END_MESSAGE_MAP()
```
Это таблица «сообщение Windows → функция-обработчик». Когда пользователь нажимает кнопку IDC_BTN_CHECK, Windows посылает сообщение BN_CLICKED, MFC находит в таблице запись и вызывает `OnBnClickedCheck`.

```cpp
DECLARE_MESSAGE_MAP()   // в .h — объявить, что таблица есть
```

```cpp
afx_msg void OnBnClickedCheck();
// afx_msg — просто пометка для читаемости (реально пустой макрос)
```

### Методы окна

```cpp
SetWindowText(_T("Заголовок"));  // установить текст окна/контрола
GetDlgItem(IDC_BTN_CHECK);       // получить указатель на контрол по ID
MoveWindow(x, y, w, h);          // переместить и изменить размер
SendMessage(EM_SETLIMITTEXT, n, 0); // послать сообщение Windows окну
```

### `_T("строка")`
Макрос: в Unicode-сборке превращает строку в `L"строка"` (широкие символы), в ANSI — оставляет `"строка"`. Нужен для поддержки обоих режимов.

### DDX (Data eXchange)
Механизм двустороннего обмена данными между C++-переменными и контролами диалога.
```cpp
DDX_Control(pDX, IDC_EDIT_OUTPUT, m_editOutput);
// Связать поле IDC_EDIT_OUTPUT в диалоге с переменной m_editOutput
```

### `DoModal()`
Открыть диалог в **модальном** режиме: блокирует родительское окно, возвращает IDOK или IDCANCEL когда диалог закрыт.

### CHARFORMAT2
Структура для форматирования текста в RichEdit:
```cpp
CHARFORMAT2 cf = {};        // обнуляем все поля
cf.cbSize = sizeof(cf);     // размер структуры (обязательно!)
cf.dwMask = CFM_COLOR;      // говорим: меняем только цвет
cf.crTextColor = RGB(255,0,0); // красный цвет
m_editOutput.SetSelectionCharFormat(cf); // применить
```

### Работа с файлами (CFile)
```cpp
CFile f;
f.Open(path, CFile::modeRead | CFile::typeBinary);  // открыть
ULONGLONG size = f.GetLength();  // размер
f.Read(buf.data(), n);           // читать n байт
f.Write(data, n);                // записать
// CFile закрывается автоматически (деструктор)
```

### MINMAXINFO
Структура Windows с минимальными/максимальными размерами окна:
```cpp
lpMMI->ptMinTrackSize.x = 480;  // минимальная ширина внешнего окна
lpMMI->ptMinTrackSize.y = 360;  // минимальная высота
```

### UTF-8, UTF-16, ACP
- **UTF-8** — кодировка Unicode, переменная ширина байт. Используется в std::string в этом проекте.
- **UTF-16** — кодировка Unicode, 2 байта на символ. Используется CString в режиме UNICODE.
- **ACP** — системная кодовая страница Windows (например Windows-1251 для русского).
- **BOM** — байты в начале файла, указывающие кодировку. UTF-8 BOM: `0xEF 0xBB 0xBF`.

Функции преобразования Windows:
```cpp
WideCharToMultiByte(CP_UTF8, ...)  // UTF-16 → UTF-8
MultiByteToWideChar(CP_UTF8, ...)  // UTF-8 → UTF-16
```

### RGB макрос
```cpp
COLORREF c = RGB(255, 0, 0);  // красный: R=255, G=0, B=0
```

---

## 11. Разбор файлов построчно

### SqlInjectionDetector.h

```cpp
inline constexpr int kSuspiciousScoreThreshold = 5;
```
- `inline` — определить прямо в заголовке (без ошибок линкера)
- `constexpr` — значение известно на этапе компиляции
- `int` — тип: целое число
- `kSuspiciousScoreThreshold` — имя: `k` в начале — константа, остальное по-английски «порог подозрительности»
- `= 5` — значение

```cpp
inline constexpr std::size_t kMaxAnalyzeInputBytes = 512 * 1024;
```
- `std::size_t` — тип для размеров (беззнаковый, не может быть отрицательным)
- `512 * 1024` — 524288 байт = 512 килобайт, компилятор вычислит сам

```cpp
struct DetectionHit {
    std::string pattern;
    std::size_t position = 0;
    int weight = 0;
    bool highRisk = false;
};
```
Структура из четырёх полей. `= 0` и `= false` — значения по умолчанию: если не задать при создании, поля будут этими значениями.

```cpp
std::vector<DetectionHit> hits;
```
Динамический массив объектов DetectionHit. `<DetectionHit>` — шаблонный параметр: «вектор, содержащий DetectionHit».

```cpp
class SqlInjectionDetector {
public:
    SqlInjectionDetector();
    DetectionReport Analyze(std::string_view input) const;
private:
    struct PatternRule { ... };
    std::vector<PatternRule> rules_;
    static std::string Normalize(std::string_view input);
    static std::vector<std::size_t> FindAllOccurrences(...);
};
```
- `class` — всё `private` по умолчанию
- `public:` — с этой строки: методы открыты для всех
- `SqlInjectionDetector()` — конструктор
- `Analyze(...) const` — метод не изменяет объект
- `private:` — поля и вспомогательные методы скрыты
- `static` — методы не нужен объект; вызов: `SqlInjectionDetector::Normalize(s)`
- `rules_` — поле с суффиксом `_` (соглашение «это поле класса»)

---

### SqlInjectionDetector.cpp

```cpp
namespace {
bool IsSpaceChar(const char c) { ... }
char ToLowerAscii(const char c) { ... }
} // namespace
```
Анонимный namespace — функции-помощники, которые нужны только в этом файле. Компилятор гарантирует, что они не будут видны снаружи.

```cpp
SqlInjectionDetector::SqlInjectionDetector() {
    rules_ = {
        {"' or", 3, true},
        ...
    };
}
```
- `SqlInjectionDetector::SqlInjectionDetector()` — реализация конструктора
- `::` — оператор разрешения области видимости: «конструктор класса SqlInjectionDetector»
- `{ {"' or", 3, true}, ... }` — инициализатор вектора: список структур PatternRule

```cpp
for (const PatternRule& rule : rules_) {
    const std::vector<std::size_t> positions =
        FindAllOccurrences(report.normalizedInput, rule.pattern);
    for (const std::size_t pos : positions) {
        report.hits.push_back({rule.pattern, pos, rule.weight, rule.highRisk});
        report.totalScore += rule.weight;
    }
}
```
Два вложенных цикла:
1. По каждому правилу из `rules_`
2. По каждой позиции, где найдено это правило
Для каждого совпадения добавляем запись в `hits` и прибавляем вес к `totalScore`.

```cpp
if (pattern.empty() || text.size() < pattern.size()) {
    return positions;
}
```
Защита от переполнения: `text.size() - pattern.size()` для беззнаковых типов даст огромное число, если pattern длиннее text. Поэтому проверяем заранее.

```cpp
for (std::size_t i = 0; i <= last; ++i) {
    if (memcmp(hay + i, ndl, plen) == 0) {
        positions.push_back(i);
    }
}
```
- `hay + i` — арифметика указателей: сдвинуть указатель на `i` байт вперёд
- `memcmp(...) == 0` — побайтовое сравнение: равны ли `plen` байт?

---

### SqlScanDlg.h

```cpp
class CSqlScanDlg : public CDialogEx {
```
`CSqlScanDlg` — наш класс диалога. `C` в начале — соглашение MFC (все классы начинаются с `C`). Наследуется от `CDialogEx`.

```cpp
enum { IDD = IDD_SQLSCAN_DIALOG };
```
`enum` с одним значением — трюк MFC: `IDD_SQLSCAN_DIALOG` — числовой ID шаблона диалога из `resource.h`. Позволяет писать `CSqlScanDlg::IDD`.

```cpp
DECLARE_MESSAGE_MAP()
```
Макрос: добавляет в класс скрытые поля/методы для работы таблицы сообщений. Обязателен если есть `BEGIN_MESSAGE_MAP` в `.cpp`.

---

### SqlScanDlg.cpp

```cpp
BOOL CSqlScanDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();  // сначала базовый класс
    SetWindowText(_T("..."));
    ...
    layoutReady_ = true;
    CRect r;
    GetClientRect(&r);         // узнать размер клиентской области
    LayoutControls(r.Width(), r.Height());
    return TRUE;
}
```
`OnInitDialog` вызывается **один раз** после создания окна. Здесь устанавливаем надписи и начальный размер.

```cpp
void CSqlScanDlg::LayoutControls(int cx, int cy) {
    if (!layoutReady_ || cx < 40 || cy < 40) return;
    const int inputH  = (avail * 55) / 100;
    const int outputH = avail - inputH;
    if (CWnd* p = GetDlgItem(IDC_EDIT_INPUT))
        p->MoveWindow(m, y, cx - 2 * m, inputH);
    ...
}
```
Ручная раскладка: при каждом изменении размера окна пересчитываем координаты всех контролов и перемещаем их через `MoveWindow(x, y, ширина, высота)`.

```cpp
void CSqlScanDlg::AppendRichText(COLORREF cr, const CString& text) {
    CHARFORMAT2 cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = cr;
    constexpr int kMaxChunk = 32000;
    ...
    m_editOutput.SetSel(n, n);               // каретка в конец
    m_editOutput.SetSelectionCharFormat(cf); // цвет
    m_editOutput.ReplaceSel(chunk);          // вставить текст
}
```
Добавить цветной текст в RichEdit. `kMaxChunk = 32000` — ограничение самого RichEdit: за один раз нестабильно работает с очень большими кусками.

```cpp
COLORREF CSqlScanDlg::ScoreToGradientColor(int score) {
    const float t = s / 100.0f;  // t в диапазоне [0, 1]
    if (t <= 0.5f) {
        // интерполяция от зелёного к жёлтому
        const float u = t / 0.5f;
        R = gR + (yR - gR) * u;
        ...
    }
    return RGB(...);
}
```
Линейная интерполяция цветов: `A + (B - A) * t` при `t` от 0 до 1 даёт плавный переход от A к B.

```cpp
std::string CSqlScanDlg::CStringToUtf8(const CString& s) {
#ifdef UNICODE
    const int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
#else
    return std::string(static_cast<LPCSTR>(s));
#endif
}
```
Двухэтапный WinAPI-паттерн:
1. Вызов с `nullptr` буфером → узнать нужный размер
2. Вызов с реальным буфером → записать результат

```cpp
void CSqlScanDlg::OnBnClickedCheck() {
    CString input;
    GetDlgItemText(IDC_EDIT_INPUT, input);  // взять текст из поля ввода
    const std::string u8 = CStringToUtf8(input);  // конвертировать в UTF-8
    const DetectionReport r = detector_.Analyze(u8);  // анализировать
    FillReportRich(r);  // вывести отчёт
}
```
Весь поток «нажали кнопку Проверить» в четыре строки.

---

### SqlScan.cpp

```cpp
CSqlScanApp theApp;
```
Глобальный объект приложения. MFC ищет именно его при старте программы. Существует всё время работы программы.

```cpp
BOOL CSqlScanApp::InitInstance() {
    CWinApp::InitInstance();
    if (!AfxInitRichEdit2()) { return FALSE; }
    SetRegistryKey(_T("SqlScanCoursework"));
    CSqlScanDlg dlg;    // создать диалог на стеке
    m_pMainWnd = &dlg;  // сказать MFC, что это главное окно
    dlg.DoModal();      // показать диалог (блокирует до закрытия)
    return FALSE;       // не запускать основной цикл MFC
}
```
`InitInstance` — «конструктор» приложения. `return FALSE` — нетипично; обычно возвращают TRUE для запуска цикла сообщений, но здесь диалог сам цикл.

---

## Быстрый словарь терминов

| Термин | Объяснение |
|--------|-----------|
| **Компилятор** | Программа, переводящая код C++ в машинный код |
| **Препроцессор** | Первый шаг компиляции: обрабатывает `#include`, `#define` |
| **Линкер** | Собирает скомпилированные `.obj` файлы в `.exe` |
| **Заголовок (.h)** | Файл с объявлениями: «что существует» |
| **Единица компиляции (.cpp)** | Файл с реализацией: «как работает» |
| **Стек** | Быстрая память для локальных переменных (освобождается автоматически) |
| **Куча** | Память для динамических объектов (`new`/`delete`) |
| **Итератор** | «Курсор» для обхода коллекции (как указатель на элемент) |
| **Шаблон (template)** | Код, параметризованный типом: `vector<int>`, `vector<string>` |
| **Деструктор** | Метод, вызываемый при уничтожении объекта (очищает ресурсы) |
| **RAII** | Ресурс привязан к объекту: открыл → деструктор закроет |
| **RVO/NRVO** | Оптимизация компилятора: не копировать возвращаемый объект |
| **Unicode** | Стандарт кодировки всех символов мира |
| **Handle (HWND)** | «Рукоятка» — идентификатор объекта Windows (окна, файла и т.д.) |
| **Message pump** | Цикл обработки сообщений Windows (клики, нажатия клавиш) |
