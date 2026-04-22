#pragma once

#ifndef VC_EXTRALEAN
/* VC_EXTRALEAN — исключает редко используемые части Windows.h (ускоряет сборку). */
#define VC_EXTRALEAN
#endif

/* targetver.h — задаёт минимальную версию Windows (SDKDDKVer.h). */
#include "targetver.h"

/* _ATL_CSTRING_EXPLICIT_CONSTRUCTORS — конструкторы CString делаются explicit, меньше скрытых преобразований. */
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
/* _AFX_ALL_WARNINGS — включить предупреждения MFC. */
#define _AFX_ALL_WARNINGS

#include <afxwin.h>   /* MFC ядро: CWinApp, CWnd, окна. */
#include <afxext.h>   /* Расширения (панели инструментов и т.д.). */
#include <afxcmn.h>   /* Общие контролы. */
#include <afxdlgs.h>  /* Диалоги выбора файла CFileDialog. */
#include <afxdialogex.h> /* CDialogEx — модальный диалог с DataExchange. */
