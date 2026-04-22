#include "pch.h"
#include "SqlScanDlg.h"

#include <Richedit.h>
#include <algorithm>
#include <vector>

namespace {

constexpr DWORD kMaxInputEditTchars  = 256 * 1024;
constexpr DWORD kMaxOutputRichTchars = 512 * 1024;
constexpr ULONGLONG kMaxFileLoadBytes = 2 * 1024 * 1024;
constexpr int kMinClientWidth  = 480;
constexpr int kMinClientHeight = 360;

} // namespace

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CSqlScanDlg::CSqlScanDlg(CWnd* pParent)
    : CDialogEx(IDD_SQLSCAN_DIALOG, pParent) {}

void CSqlScanDlg::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_OUTPUT, m_editOutput);
}

BEGIN_MESSAGE_MAP(CSqlScanDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_CHECK, &CSqlScanDlg::OnBnClickedCheck)
    ON_BN_CLICKED(IDC_BTN_LOAD,  &CSqlScanDlg::OnBnClickedLoad)
    ON_BN_CLICKED(IDC_BTN_SAVE,  &CSqlScanDlg::OnBnClickedSave)
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

BOOL CSqlScanDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    SetWindowText(_T("SqlScan — проверка SQL-инъекций"));

    if (CWnd* p = GetDlgItem(IDC_STATIC_IN))  p->SetWindowText(_T("Введите строку для проверки:"));
    if (CWnd* p = GetDlgItem(IDC_STATIC_OUT)) p->SetWindowText(_T("Результат:"));
    if (CWnd* p = GetDlgItem(IDC_BTN_CHECK))  p->SetWindowText(_T("Проверить"));
    if (CWnd* p = GetDlgItem(IDC_BTN_LOAD))   p->SetWindowText(_T("Загрузить из файла..."));
    if (CWnd* p = GetDlgItem(IDC_BTN_SAVE))   p->SetWindowText(_T("Сохранить отчёт..."));

    if (CWnd* pIn = GetDlgItem(IDC_EDIT_INPUT)) {
        pIn->SendMessage(EM_SETLIMITTEXT, static_cast<WPARAM>(kMaxInputEditTchars), 0);
    }
    m_editOutput.LimitText(kMaxOutputRichTchars);

    layoutReady_ = true;
    CRect r;
    GetClientRect(&r);
    LayoutControls(r.Width(), r.Height());
    return TRUE;
}

void CSqlScanDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
    CDialogEx::OnGetMinMaxInfo(lpMMI);
    CRect r(0, 0, kMinClientWidth, kMinClientHeight);
    AdjustWindowRectEx(&r, GetStyle(), FALSE, GetExStyle());
    lpMMI->ptMinTrackSize.x = r.Width();
    lpMMI->ptMinTrackSize.y = r.Height();
}

void CSqlScanDlg::OnSize(UINT nType, int cx, int cy) {
    CDialogEx::OnSize(nType, cx, cy);
    if (nType == SIZE_MINIMIZED) return;
    LayoutControls(cx, cy);
}

void CSqlScanDlg::LayoutControls(int cx, int cy) {
    if (!layoutReady_ || cx < 40 || cy < 40) return;

    const int m    = 8;
    const int lblH = 14;
    const int btnH = 28;
    const int btnW1 = 85;
    const int btnW2 = 125;
    const int btnW3 = 135;
    const int gap   = 8;

    const int reserved = m + lblH + 4 + m + btnH + m + lblH + 4 + m;
    int avail = cy - reserved;
    if (avail < 60) avail = 60;

    const int inputH  = (avail * 55) / 100;
    const int outputH = avail - inputH;

    int y = m;
    if (CWnd* p = GetDlgItem(IDC_STATIC_IN))  p->MoveWindow(m, y, cx - 2 * m, lblH);
    y += lblH + 4;

    if (CWnd* p = GetDlgItem(IDC_EDIT_INPUT)) p->MoveWindow(m, y, cx - 2 * m, inputH);
    y += inputH + m;

    int xb = m;
    if (CWnd* p = GetDlgItem(IDC_BTN_CHECK)) { p->MoveWindow(xb, y, btnW1, btnH); xb += btnW1 + gap; }
    if (CWnd* p = GetDlgItem(IDC_BTN_LOAD))  { p->MoveWindow(xb, y, btnW2, btnH); xb += btnW2 + gap; }
    if (CWnd* p = GetDlgItem(IDC_BTN_SAVE))  { p->MoveWindow(xb, y, btnW3, btnH); }
    y += btnH + m;

    if (CWnd* p = GetDlgItem(IDC_STATIC_OUT))  p->MoveWindow(m, y, cx - 2 * m, lblH);
    y += lblH + 4;

    if (CWnd* p = GetDlgItem(IDC_EDIT_OUTPUT)) p->MoveWindow(m, y, cx - 2 * m, outputH);
}

std::string CSqlScanDlg::CStringToUtf8(const CString& s) {
#ifdef UNICODE
    if (s.IsEmpty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), len, nullptr, nullptr);
    return out;
#else
    return std::string(static_cast<LPCSTR>(s));
#endif
}

CString CSqlScanDlg::Utf8ToCString(const std::string& s) {
#ifdef UNICODE
    if (s.empty()) return CString();
    const int byteLen = static_cast<int>(s.size());
    // MB_ERR_INVALID_CHARS lets us detect non-UTF-8 bytes and fall back to ACP
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), byteLen, nullptr, 0);
    if (wlen <= 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, s.data(), byteLen, nullptr, 0);
        if (wlen <= 0) return CString();
        CStringW w;
        LPWSTR p = w.GetBuffer(wlen);
        MultiByteToWideChar(CP_ACP, 0, s.data(), byteLen, p, wlen);
        w.ReleaseBuffer(wlen);
        return CString(w);
    }
    CStringW w;
    LPWSTR p = w.GetBuffer(wlen);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), byteLen, p, wlen);
    w.ReleaseBuffer(wlen);
    return CString(w);
#else
    return CString(s.c_str());
#endif
}

COLORREF CSqlScanDlg::ScoreToGradientColor(int score) {
    int s = score;
    if (s < 0)   s = 0;
    if (s > 100) s = 100;
    const float t = s / 100.0f;

    const float gR = 25.0f,  gG = 150.0f, gB = 55.0f;
    const float yR = 255.0f, yG = 210.0f, yB = 0.0f;
    const float rR = 215.0f, rG = 35.0f,  rB = 30.0f;

    float R, G, B;
    if (t <= 0.5f) {
        const float u = t / 0.5f;
        R = gR + (yR - gR) * u;
        G = gG + (yG - gG) * u;
        B = gB + (yB - gB) * u;
    } else {
        const float u = (t - 0.5f) / 0.5f;
        R = yR + (rR - yR) * u;
        G = yG + (rG - yG) * u;
        B = yB + (rB - yB) * u;
    }
    return RGB(
        static_cast<BYTE>(R + 0.5f),
        static_cast<BYTE>(G + 0.5f),
        static_cast<BYTE>(B + 0.5f));
}

void CSqlScanDlg::AppendRichText(COLORREF cr, const CString& text) {
    CHARFORMAT2 cf = {};
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR;
    cf.crTextColor = cr;
    // RichEdit becomes unstable with very large single ReplaceSel calls
    constexpr int kMaxChunk = 32000;
    const int total = text.GetLength();
    for (int off = 0; off < total; off += kMaxChunk) {
        const int take     = (std::min)(kMaxChunk, total - off); // (std::min) avoids the Windows.h min macro
        const CString chunk = text.Mid(off, take);
        const long n       = m_editOutput.GetWindowTextLength();
        m_editOutput.SetSel(n, n);
        m_editOutput.SetSelectionCharFormat(cf);
        m_editOutput.ReplaceSel(chunk);
    }
}

namespace {
constexpr std::size_t kMaxDisplayNormalizedBytes = 64 * 1024;
} // namespace

void CSqlScanDlg::AppendNormalizedWithHighlights(
    const std::string& normalized,
    const std::vector<DetectionHit>& hits) {
    const COLORREF kText = RGB(0, 0, 0);
    const COLORREF kHigh = RGB(200, 30, 30);

    if (normalized.size() > kMaxDisplayNormalizedBytes) {
        AppendRichText(kText, Utf8ToCString(normalized.substr(0, kMaxDisplayNormalizedBytes)));
        CString note;
        note.Format(
            _T("\r\n[… не показано ещё %zu байт нормализованной строки: вывод ограничен %zu КБ …]\r\n"),
            normalized.size() - kMaxDisplayNormalizedBytes,
            kMaxDisplayNormalizedBytes / 1024);
        AppendRichText(RGB(100, 100, 100), note);
        return;
    }

    std::vector<std::pair<size_t, size_t>> segs;
    for (const DetectionHit& h : hits) {
        if (!h.highRisk) continue;
        const size_t patLen = h.pattern.size();
        if (patLen > normalized.size() || h.position > normalized.size() - patLen) continue;
        segs.emplace_back(h.position, h.position + patLen);
    }
    if (segs.empty()) {
        AppendRichText(kText, Utf8ToCString(normalized));
        return;
    }

    std::sort(segs.begin(), segs.end());
    std::vector<std::pair<size_t, size_t>> merged;
    for (const auto& p : segs) {
        if (merged.empty() || p.first > merged.back().second) {
            merged.push_back(p);
        } else {
            merged.back().second = (std::max)(merged.back().second, p.second);
        }
    }

    const size_t n = normalized.size();
    size_t pos = 0;
    for (const auto& iv : merged) {
        if (iv.first >= n) break;
        const size_t spanEnd = (std::min)(iv.second, n);
        if (pos < iv.first) {
            AppendRichText(kText, Utf8ToCString(normalized.substr(pos, iv.first - pos)));
        }
        if (iv.first < spanEnd) {
            AppendRichText(kHigh, Utf8ToCString(normalized.substr(iv.first, spanEnd - iv.first)));
        }
        pos = (std::max)(pos, spanEnd);
    }
    if (pos < n) {
        AppendRichText(kText, Utf8ToCString(normalized.substr(pos)));
    }
}

void CSqlScanDlg::FillReportRich(const DetectionReport& r) {
    m_editOutput.SetWindowText(_T(""));

    const COLORREF kText     = RGB(0, 0, 0);
    const COLORREF kHighRisk = RGB(200, 30, 30);

    if (r.inputTruncated) {
        CString trunc;
        trunc.Format(
            _T("Внимание: для анализа взяты только первые %zu КБ из %zu байт входа.\r\n"),
            static_cast<std::size_t>(kMaxAnalyzeInputBytes / 1024),
            r.originalInputBytes);
        AppendRichText(RGB(128, 64, 0), trunc);
    }

    AppendRichText(kText, _T("Подозрительно: "));
    AppendRichText(kText, r.suspicious ? _T("ДА") : _T("НЕТ"));
    AppendRichText(kText, _T("\r\nСумма весов: "));

    CString num;
    num.Format(_T("%d"), r.totalScore);
    AppendRichText(ScoreToGradientColor(r.totalScore), num);
    AppendRichText(
        kText,
        _T("  (цвет суммы: чем меньше — тем зеленее, середина — жёлто-оранжевый, чем больше — тем краснее; ")
        _T("шкала 0…100, при сумме > 100 цвет как у 100)\r\n\r\n"));

    AppendRichText(kText, _T("--- Как считается вес ---\r\n"));
    AppendRichText(kText, _T("Вес — условная «сила» сигнатуры (1–5): чем выше, тем типичнее опасный фрагмент SQL-инъекции.\r\n"));
    AppendRichText(kText, _T("За каждое вхождение паттерна к сумме прибавляется его вес; один паттерн может встретиться несколько раз.\r\n"));

    CString thr;
    thr.Format(
        _T("Строка считается подозрительной, если найден хотя бы один паттерн «высокий риск» или сумма весов >= %d.\r\n"),
        kSuspiciousScoreThreshold);
    AppendRichText(kText, thr);

    int sumCheck = 0;
    for (const DetectionHit& h : r.hits) sumCheck += h.weight;
    CString verify;
    verify.Format(_T("Проверка: сумма весов по списку = %d.\r\n"), sumCheck);
    AppendRichText(kText, verify);

    AppendRichText(kText, _T("\r\nНормализованная строка (красным — фрагменты высокого риска):\r\n"));
    AppendNormalizedWithHighlights(r.normalizedInput, r.hits);
    AppendRichText(kText, _T("\r\n\r\nСовпадения:\r\n"));

    if (r.hits.empty()) {
        AppendRichText(kText, _T("— нет —\r\n"));
        return;
    }
    for (const DetectionHit& h : r.hits) {
        CString line;
        if (h.highRisk) {
            line.Format(
                _T("  • паттерн \"%hs\", позиция %llu, вес %d — высокий риск\r\n"),
                h.pattern.c_str(),
                static_cast<unsigned long long>(h.position),
                h.weight);
            AppendRichText(kHighRisk, line);
        } else {
            line.Format(
                _T("  • паттерн \"%hs\", позиция %llu, вес %d\r\n"),
                h.pattern.c_str(),
                static_cast<unsigned long long>(h.position),
                h.weight);
            AppendRichText(kText, line);
        }
    }
}

BOOL CSqlScanDlg::LoadTextFile(const CString& path, CString& out, CString& err) {
    CFile f;
    if (!f.Open(path, CFile::modeRead | CFile::typeBinary)) {
        err = _T("Не удалось открыть файл для чтения.");
        return FALSE;
    }

    const ULONGLONG n = f.GetLength();
    if (n > kMaxFileLoadBytes) {
        err = _T("Файл слишком большой (лимит 2 МБ).");
        return FALSE;
    }
    if (n == 0) {
        out.Empty();
        return TRUE;
    }

    std::vector<BYTE> buf(static_cast<size_t>(n));
    f.Read(buf.data(), static_cast<UINT>(n));

    for (BYTE& b : buf) {
        if (b == 0) b = 0x20; // NUL bytes would terminate C-strings prematurely
    }

    size_t off = 0;
    // Skip UTF-8 BOM (EF BB BF) if present
    if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
        off = 3;
    }

    const LPCCH pBytes  = reinterpret_cast<LPCCH>(buf.data() + off);
    const int   byteLen = static_cast<int>(n - off);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, pBytes, byteLen, nullptr, 0);
    if (wlen == 0 && byteLen > 0) {
        wlen = MultiByteToWideChar(CP_ACP, 0, pBytes, byteLen, nullptr, 0);
        CStringW w;
        LPWSTR wp = w.GetBuffer(wlen);
        MultiByteToWideChar(CP_ACP, 0, pBytes, byteLen, wp, wlen);
        w.ReleaseBuffer(wlen);
        out = CString(w);
        return TRUE;
    }

    CStringW w;
    LPWSTR wp = w.GetBuffer(wlen);
    MultiByteToWideChar(CP_UTF8, 0, pBytes, byteLen, wp, wlen);
    w.ReleaseBuffer(wlen);
    out = CString(w);
    return TRUE;
}

BOOL CSqlScanDlg::SaveTextFileUtf8(const CString& path, const CString& text) {
    CFile f;
    if (!f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
        return FALSE;
    }

    const BYTE bom[] = {0xEF, 0xBB, 0xBF}; // UTF-8 BOM for Windows compatibility
    f.Write(bom, 3);

#ifdef UNICODE
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return TRUE;
    CStringA utf8;
    LPSTR p = utf8.GetBuffer(n - 1);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, p, n, nullptr, nullptr);
    utf8.ReleaseBuffer(n - 1);
    f.Write(utf8.GetString(), static_cast<UINT>(n - 1));
#else
    f.Write(text, text.GetLength());
#endif
    return TRUE;
}

void CSqlScanDlg::OnBnClickedCheck() {
    CString input;
    GetDlgItemText(IDC_EDIT_INPUT, input);
    const std::string u8     = CStringToUtf8(input);
    const DetectionReport r  = detector_.Analyze(u8);
    FillReportRich(r);
}

void CSqlScanDlg::OnBnClickedLoad() {
    static constexpr LPCTSTR kLoadFilter =
        _T("Текст и исходники|")
        _T("*.txt;*.log;*.csv;*.tsv;*.sql;*.json;*.xml;*.yaml;*.yml;*.ini;*.cfg;*.conf;")
        _T("*.htm;*.html;*.md;*.rst;*.c;*.cc;*.cpp;*.cxx;*.h;*.hpp;*.hxx;*.cs;*.java;*.py;*.rb;*.php;*.js;*.ts;*.mjs;*.css;*.scss;*.bat;*.cmd;*.ps1;*.sh;*.properties;*.env|")
        _T("Все файлы (*.*)|*.*||");

    CFileDialog dlg(TRUE, _T("txt"), nullptr,
        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kLoadFilter, this);
    if (dlg.DoModal() != IDOK) return;

    CString err, text;
    if (!LoadTextFile(dlg.GetPathName(), text, err)) {
        AfxMessageBox(err, MB_ICONWARNING);
        return;
    }
    if (text.GetLength() > static_cast<int>(kMaxInputEditTchars)) {
        text = text.Left(static_cast<int>(kMaxInputEditTchars));
        AfxMessageBox(
            _T("Текст обрезан до 256 КБ символов: иначе поле ввода не справляется с отрисовкой."),
            MB_ICONINFORMATION);
    }
    SetDlgItemText(IDC_EDIT_INPUT, text);
}

void CSqlScanDlg::OnBnClickedSave() {
    CString text;
    m_editOutput.GetWindowText(text);

    static constexpr LPCTSTR kSaveFilter =
        _T("Отчёт UTF-8 (*.txt)|*.txt|Текст (*.txt;*.log;*.md;*.csv)|*.txt;*.log;*.md;*.csv|Все файлы (*.*)|*.*||");

    CFileDialog dlg(FALSE, _T("txt"), _T("report.txt"),
        OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, kSaveFilter, this);
    if (dlg.DoModal() != IDOK) return;

    if (!SaveTextFileUtf8(dlg.GetPathName(), text)) {
        AfxMessageBox(_T("Не удалось сохранить файл."), MB_ICONERROR);
    }
}
