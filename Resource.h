#pragma once
/* Файл генерируется/правится вместе с .rc (ресурсы диалога). Числа — уникальные ID в пределах проекта. */

/* IDD_ — идентификатор **диалогового шаблона** (окно с кнопками). В коде: CDialogEx(IDD_SQLSCAN_DIALOG). */
#define IDD_SQLSCAN_DIALOG 101

/* IDC_ — идентификаторы **контролов внутри диалога** (поля, кнопки, статики). */
#define IDC_EDIT_INPUT 1001   /* Многострочное поле ввода (обычный EDIT). */
#define IDC_EDIT_OUTPUT 1002  /* Поле вывода отчёта (Rich Edit в .rc). */
#define IDC_BTN_CHECK 1003    /* Кнопка «Проверить». */
#define IDC_BTN_LOAD 1004     /* «Загрузить из файла». */
#define IDC_BTN_SAVE 1005     /* «Сохранить отчёт». */
#define IDC_STATIC_IN 1006    /* Подпись «Введите строку...». */
#define IDC_STATIC_OUT 1007   /* Подпись «Результат:». */

/* Блок только для редактора ресурсов Visual Studio (не для компилятора обычного кода). */
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
/* Следующие свободные номера при добавлении новых ресурсов/контролов в дизайнере. */
#define _APS_NEXT_RESOURCE_VALUE 102
#define _APS_NEXT_COMMAND_VALUE 40001
#define _APS_NEXT_CONTROL_VALUE 1008
#define _APS_NEXT_SYMED_VALUE 102
#endif
#endif
