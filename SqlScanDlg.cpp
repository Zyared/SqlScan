#include "pch.h"
#include "SqlScanDlg.h"
#include "ReportIO.h"
#include <algorithm>
#include <array>
#include <richedit.h>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>

namespace {
constexpr std::size_t maxEditUnits = 256 * 1024;

// The classic multiline edit control needs CRLF, even when the original file uses LF or CR.
std::wstring FilePreview(const std::wstring &input, bool &limited) {
    std::wstring preview;
    preview.reserve((std::min)(input.size(), maxEditUnits));
    std::size_t i = 0;
    while (i < input.size() && preview.size() < maxEditUnits) {
        const wchar_t c = input[i];
        if (c == L'\r' || c == L'\n') {
            if (preview.size() + 2 > maxEditUnits)
                break;
            preview += L"\r\n";
            if (c == L'\r' && i + 1 < input.size() && input[i + 1] == L'\n')
                ++i;
        } else {
            if (c >= 0xd800 && c <= 0xdbff && preview.size() + 2 > maxEditUnits)
                break;
            preview += c;
        }
        ++i;
    }
    limited = i < input.size();
    return preview;
}
std::string Field(CWnd &w, int id) {
    CString s;
    w.GetDlgItemText(id, s);
    return io::Utf8(s.GetString());
}
void Set(CWnd &w, int id, const std::string &s) { w.SetDlgItemText(id, io::Wide(s).c_str()); }
COLORREF ScoreColor(int score) {
    // Preserve the coursework scale: green at zero, gold at 50, red from 100 onward.
    const int value = std::clamp(score, 0, 100);
    const auto green = RGB(32, 112, 45), gold = RGB(145, 111, 0), red = RGB(173, 44, 39);
    const auto from = value < 50 ? green : gold;
    const auto to = value < 50 ? gold : red;
    const int step = value < 50 ? value : value - 50;
    auto blend = [step](int a, int b) { return a + (b - a) * step / 50; };
    return RGB(blend(GetRValue(from), GetRValue(to)), blend(GetGValue(from), GetGValue(to)),
               blend(GetBValue(from), GetBValue(to)));
}
void Error(const std::exception &e) { AfxMessageBox(io::Wide(e.what()).c_str(), MB_ICONWARNING); }
std::string Yes(bool value) { return value ? "Да" : "Нет"; }
class Form : public CDialogEx {
  public:
    using Action = std::function<void(Form &)>;
    Form(int id, CWnd *parent, Action init, Action ok = {}, Action extra = {})
        : CDialogEx(id, parent), init_(std::move(init)), ok_(std::move(ok)), extra_(std::move(extra)) {}
    CComboBox &Combo(int id) { return *static_cast<CComboBox *>(GetDlgItem(id)); }
    BOOL OnInitDialog() override {
        CDialogEx::OnInitDialog();
        init_(*this);
        return TRUE;
    }
    void OnOK() override {
        try {
            if (ok_)
                ok_(*this);
            CDialogEx::OnOK();
        } catch (const std::exception &e) {
            Error(e);
        }
    }
    BOOL OnCommand(WPARAM w, LPARAM l) override {
        if ((LOWORD(w) == IDC_TEST || LOWORD(w) == IDC_EXPORT) && HIWORD(w) == BN_CLICKED) {
            try {
                if (extra_)
                    extra_(*this);
            } catch (const std::exception &e) {
                Error(e);
            }
            return TRUE;
        }
        return CDialogEx::OnCommand(w, l);
    }

  private:
    Action init_, ok_, extra_;
};
void Choice(Form &f, int id, const std::vector<std::wstring> &labels, int index) {
    auto &cb = f.Combo(id);
    for (auto &s : labels)
        cb.AddString(s.c_str());
    cb.SetCurSel(index);
}
void Export(CWnd *parent, const wchar_t *basename,
            const std::function<void(std::ostream &, const std::string &)> &render) {
    CFileDialog dlg(FALSE, L"txt", basename, OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                    L"Текст (*.txt)|*.txt|CSV (*.csv)|*.csv|HTML (*.html)|*.html||", parent);
    if (dlg.DoModal() != IDOK)
        return;
    std::filesystem::path path(dlg.GetPathName().GetString());
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    std::string format = ext == L".html" || ext == L".htm" ? "html" : ext == L".csv" ? "csv" : "txt";
    io::Write(path, [&](std::ostream &out) {
        out << "\xef\xbb\xbf";
        render(out, format);
    });
}
void Grid(std::ostream &out, const Row &headers, const Rows &rows, const std::string &format,
          std::size_t riskColumn = SIZE_MAX, bool highFlag = false) {
    bool html = format == "html", csv = format == "csv";
    if (html)
        out << "<table>";
    auto line = [&](const Row &r, bool head) {
        if (html) {
            const char *style = "";
            if (!head && riskColumn < r.size()) {
                const auto &risk = r[riskColumn];
                if (risk == "Критический")
                    style = " class=\"critical\"";
                else if (risk == "Высокий" || risk == "Средний" || risk == "Подозрительно" || (highFlag && risk == "1"))
                    style = " class=\"elevated\"";
            }
            out << "<tr" << style << '>';
        }
        for (std::size_t i = 0; i < r.size(); ++i) {
            if (html)
                out << (head ? "<th>" : "<td>") << io::Html(r[i]) << (head ? "</th>" : "</td>");
            else {
                if (i)
                    out << (csv ? "," : "\t");
                out << (csv ? io::Csv(r[i]) : r[i]);
            }
        }
        out << (html ? "</tr>" : "\r\n");
    };
    line(headers, true);
    for (const auto &r : rows)
        line(r, false);
    if (html)
        out << "</table>";
}
constexpr char archiveHtml[] =
    "<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\"><title>Журнал SqlScan</title>"
    "<style>body{font:16px system-ui}td,th{padding:.4em;text-align:left;border-bottom:1px solid #ccc}"
    ".elevated{background:#ffe2cf}.critical{background:#ffc9c7;font-weight:bold}</style><body>";
constexpr char archiveEnd[] = "</body></html>";
bool HasLineScores(const Row &summary) {
    return !summary[8].empty() && !summary[9].empty() && summary[9] == summary[14];
}
constexpr char missingLineScores[] = "В этом архиве недостаточно данных для построчных оценок.";
std::string LineVerdict(const std::string &flag) { return flag == "1" ? "Подозрительно" : "Чисто"; }
void StoredReport(std::ostream &out, const Row &summary, const Rows &hits, Rows lines, const std::string &format) {
    const Row head = {"ID", "Время", "Источник", "Имя", "Баллы", "Порог", "Подозрительно", "Риск", "Строк",
                      "Совпадений", "Байт входа", "Байт анализа", "Вход ограничен", "SHA-256", "Сохранено совпадений"};
    const Row hitHead = {"Строка", "Позиция", "Шаблон", "Вес", "Высокий риск", "Контекст", "Категория"};
    const bool html = format == "html";
    if (html)
        out << archiveHtml;
    Grid(out, head, {summary}, format, 7);
    out << (html ? "<h2>Построчные оценки</h2>" : "\r\n");
    if (HasLineScores(summary)) {
        for (auto &row : lines)
            row[2] = LineVerdict(row[2]);
        Grid(out, {"Строка", "Баллы", "Вердикт"}, lines, format, 2);
    } else {
        out << (html ? "<p>" : "") << missingLineScores << (html ? "</p>" : "\r\n");
    }
    out << (html ? "<h2>Совпадения</h2>" : "\r\n");
    Grid(out, hitHead, hits, format, 4, true);
    if (html)
        out << archiveEnd;
}
const Row historyHead = {"ID", "Время", "Источник", "Имя", "Подозрительно", "Баллы", "Риск"};
} // namespace
BEGIN_MESSAGE_MAP(ActivityChart, CWnd)
ON_WM_PAINT()
END_MESSAGE_MAP()
void ActivityChart::OnPaint() {
    CPaintDC dc(this);
    CRect r;
    GetClientRect(&r);
    dc.FillSolidRect(r, RGB(249, 250, 251));
    dc.SetBkMode(TRANSPARENT);
    dc.TextOutW(10, 5, L"Проверки по дням · синие — все, красные — подозрительные");
    if (days.empty())
        return;
    long long maxValue = 1;
    for (const auto &day : days)
        maxValue = (std::max)(maxValue, day.total);
    int slot = (std::max)(1, (r.Width() - 24) / static_cast<int>(days.size()));
    int height = (std::max)(5, r.Height() - 58);
    int x = 12;
    for (auto iter = days.rbegin(); iter != days.rend(); ++iter, x += slot) {
        int width = (std::max)(2, (std::min)(slot - 2, 45));
        auto bar = [&](long long n, int dx, COLORREF c) {
            int h = static_cast<int>(static_cast<double>(n) / maxValue * height);
            dc.FillSolidRect(x + dx, 30 + height - h, (std::max)(1, width / 2), h, c);
        };
        bar(iter->total, 0, RGB(49, 107, 157));
        bar(iter->suspicious, width / 2, RGB(177, 72, 65));
        if (slot >= 48)
            dc.TextOutW(x, 32 + height, iter->label.c_str());
    }
}
CSqlScanDlg::CSqlScanDlg(CWnd *parent) : CDialogEx(IDD_SQLSCAN_DIALOG, parent) {}
void CSqlScanDlg::DoDataExchange(CDataExchange *dx) {
    CDialogEx::DoDataExchange(dx);
    DDX_Control(dx, IDC_TABS, tabs_);
    DDX_Control(dx, IDC_TABLE, table_);
    DDX_Control(dx, IDC_EDIT_OUTPUT, output_);
    DDX_Control(dx, IDC_CHART, chart_);
}
BEGIN_MESSAGE_MAP(CSqlScanDlg, CDialogEx)
ON_WM_SIZE()
ON_WM_GETMINMAXINFO()
ON_NOTIFY(TCN_SELCHANGE, IDC_TABS, &CSqlScanDlg::OnTab)
ON_NOTIFY(NM_DBLCLK, IDC_TABLE, &CSqlScanDlg::OnTableOpen)
ON_NOTIFY(LVN_COLUMNCLICK, IDC_TABLE, &CSqlScanDlg::OnSort)
ON_CONTROL_RANGE(BN_CLICKED, IDC_ACTION1, IDC_PAGE_NEXT, &CSqlScanDlg::OnAction)
ON_BN_CLICKED(IDC_BTN_CHECK, &CSqlScanDlg::OnScan)
ON_BN_CLICKED(IDC_BTN_LOAD, &CSqlScanDlg::OnLoad)
ON_BN_CLICKED(IDC_NEW_INPUT, &CSqlScanDlg::OnNewInput)
ON_BN_CLICKED(IDC_BTN_SAVE, &CSqlScanDlg::OnExport)
ON_EN_CHANGE(IDC_EDIT_INPUT, &CSqlScanDlg::OnChanged)
ON_COMMAND(ID_CONNECT, &CSqlScanDlg::OnConnect)
ON_COMMAND(ID_OFFLINE, &CSqlScanDlg::OnOffline)
ON_COMMAND(ID_ABOUT_SCAN, &CSqlScanDlg::OnAbout)
ON_COMMAND(ID_QUIT, &CSqlScanDlg::OnCancel)
END_MESSAGE_MAP()
BOOL CSqlScanDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();
    for (auto s : {L"Проверка", L"Журнал", L"Сигнатуры", L"Статистика"})
        tabs_.InsertItem(tabs_.GetItemCount(), s);
    tabs_.SetCurSel(0);
    table_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    output_.LimitText(512 * 1024);
    GetDlgItem(IDC_EDIT_INPUT)->SendMessage(EM_SETLIMITTEXT, maxEditUnits);
    ready_ = true;
    try {
        wchar_t *env = nullptr;
        std::size_t n = 0;
        _wdupenv_s(&env, &n, L"SQLSCAN_CONNECTION");
        if (env) {
            connection_ = env;
            free(env);
        } else
            connection_ = SavedConnection();
        if (!connection_.empty())
            Connect();
    } catch (const std::exception &e) {
        notice_ = "БД недоступна · " + std::string(e.what());
    }
    Refresh();
    return TRUE;
}
void CSqlScanDlg::Connect() {
    db_.Open(connection_);
    try {
        detector_ = rules_.Detector();
    } catch (...) {
        db_.Close();
        throw;
    }
}
void CSqlScanDlg::Status() {
    std::string s = db_.Connected() ? "PostgreSQL подключена" : "Автономный режим";
    s += "  ·  Активных правил: " + std::to_string(detector_.Rules().size());
    if (!notice_.empty())
        s += "  ·  " + notice_;
    Set(*this, IDC_STATUS, s);
}
void CSqlScanDlg::OnOffline() {
    try {
        SaveConnection(L"");
        connection_.clear();
        UseBuiltinRules();
        page_ = 0;
        notice_ = "Проверки доступны со встроенным набором";
        Refresh();
    } catch (const std::exception &e) {
        Error(e);
    }
}
void CSqlScanDlg::OnConnect() {
    auto value = [](Form &f) {
        CString entry;
        f.GetDlgItemText(IDC_CONN, entry);
        std::wstring s = entry.GetString();
        if (s.empty())
            throw std::runtime_error("Введите DSN или строку подключения");
        return s.find(L'=') == s.npos ? L"DSN=" + s : s;
    };
    Form form(
        IDD_CONNECT, this,
        [&](Form &f) {
            f.SetDlgItemText(IDC_CONN, connection_.empty() ? L"DSN=SqlScan" : connection_.c_str());
        },
        [&](Form &f) {
            auto s = value(f);
            DbConnection probe;
            probe.Open(s);
            auto detector = RuleRepository(probe).Detector();
            SaveConnection(s);
            db_.Swap(probe);
            detector_ = std::move(detector);
            connection_ = std::move(s);
        },
        [&](Form &f) {
            DbConnection probe;
            probe.Open(value(f));
            auto det = RuleRepository(probe).Detector();
            AfxMessageBox(
                (L"Подключение успешно. Активных правил: " + std::to_wstring(det.Rules().size())).c_str());
        });
    if (form.DoModal() == IDOK) {
        page_ = 0;
        notice_.clear();
        Refresh();
    }
}
void CSqlScanDlg::OnAbout() {
    AfxMessageBox(L"SqlScan 2.0\nПострочная проверка текста по сигнатурам. Баллы и риск помогают выбрать "
                  L"строки для ручного разбора.");
}
void CSqlScanDlg::OnGetMinMaxInfo(MINMAXINFO *m) {
    CDialogEx::OnGetMinMaxInfo(m);
    m->ptMinTrackSize = {730, 480};
}
void CSqlScanDlg::OnSize(UINT type, int x, int y) {
    CDialogEx::OnSize(type, x, y);
    if (ready_ && type != SIZE_MINIMIZED)
        Layout();
}
void CSqlScanDlg::Layout() {
    CRect r;
    GetClientRect(&r);
    int w = r.Width(), h = r.Height(), t = Tab(), m = 12;
    auto move = [&](int id, bool visible, int x, int y, int width, int height) {
        auto c = GetDlgItem(id);
        c->ShowWindow(visible ? SW_SHOW : SW_HIDE);
        if (visible)
            c->MoveWindow(x, y, (std::max)(1, width), (std::max)(1, height));
    };
    move(IDC_TABS, true, m, 6, w - 2 * m, 31);
    move(IDC_STATUS, true, m, h - 27, w - 2 * m, 23);
    int inputH = (std::max)(70, (h - 190) * 42 / 100), buttonsY = 65 + inputH;
    SetDlgItemText(IDC_STATIC_IN, previewOnly_
        ? L"Файл · показано начало только для чтения. Анализ использует загруженный файл с учётом лимита."
        : L"Текст для проверки");
    move(IDC_STATIC_IN, t == 0, m, 42, w - 2 * m, 20);
    move(IDC_EDIT_INPUT, t == 0, m, 62, w - 2 * m, inputH);
    move(IDC_BTN_CHECK, t == 0, m, buttonsY, 112, 30);
    move(IDC_BTN_LOAD, t == 0, m + 120, buttonsY, 150, 30);
    move(IDC_BTN_SAVE, t == 0, m + 278, buttonsY, 160, 30);
    move(IDC_NEW_INPUT, t == 0 && previewOnly_, m + 446, buttonsY, 130, 30);
    move(IDC_STATIC_OUT, t == 0, m, buttonsY + 38, w - 2 * m, 20);
    move(IDC_EDIT_OUTPUT, t == 0, m, buttonsY + 59, w - 2 * m, h - buttonsY - 95);
    for (int i = 0; i < 4; ++i)
        move(IDC_ACTION1 + i, (t == 1 || t == 2), m + 136 * i, 43, 128, 29);
    int bottom = t == 1 ? h - 69 : t == 3 ? (h * 57 / 100) : h - 38;
    move(IDC_TABLE, t != 0, m, t == 3 ? 47 : 80, w - 2 * m, bottom - (t == 3 ? 47 : 80));
    move(IDC_CHART, t == 3, m, bottom + 7, w - 2 * m, h - bottom - 44);
    move(IDC_PAGE_PREV, t == 1, m, h - 64, 84, 27);
    move(IDC_PAGE_NEXT, t == 1, m + 90, h - 64, 84, 27);
    move(IDC_PAGE_LABEL, t == 1, m + 183, h - 59, w - 210, 21);
}
void CSqlScanDlg::SetRows(const Rows &data, const std::vector<std::pair<std::wstring, int>> &cols) {
    table_.SetRedraw(FALSE);
    table_.DeleteAllItems();
    while (table_.DeleteColumn(0)) {
    }
    int i = 0;
    for (auto &c : cols)
        table_.InsertColumn(i++, c.first.c_str(), LVCFMT_LEFT, c.second);
    for (auto &row : data) {
        int index = table_.InsertItem(table_.GetItemCount(), io::Wide(row[0]).c_str());
        for (std::size_t j = 1; j < cols.size() && j < row.size(); ++j)
            table_.SetItemText(index, (int)j, io::Wide(row[j]).c_str());
    }
    table_.SetRedraw(TRUE);
    table_.Invalidate();
}
void CSqlScanDlg::UseBuiltinRules() {
    db_.Close();
    detector_ = SqlInjectionDetector();
}
void CSqlScanDlg::LoadTab(int t) {
    rows_.clear();
    chart_.days.clear();
    more_ = false;
    if (t == 1) {
        for (auto [id, s] :
             std::initializer_list<std::pair<int, const wchar_t *>>{{IDC_ACTION1, L"Фильтры…"},
                                                                    {IDC_ACTION2, L"Открыть"},
                                                                    {IDC_ACTION3, L"Удалить"},
                                                                    {IDC_ACTION4, L"Экспорт выборки…"}})
            SetDlgItemText(id, s);
        if (db_.Connected()) {
            rows_ = scans_.History(filter_, page_ * 50, 51);
            more_ = rows_.size() > 50;
            if (more_)
                rows_.pop_back();
        }
        SetRows(rows_, {{L"ID", 55},
                        {L"Время", 155},
                        {L"Источник", 85},
                        {L"Имя", 180},
                        {L"Подозрительно", 110},
                        {L"Баллы", 65},
                        {L"Риск", 95}});
        Set(*this, IDC_PAGE_LABEL,
            "Страница " + std::to_string(page_ + 1) + " · Записей: " + std::to_string(rows_.size()));
    } else if (t == 2) {
        for (auto [id, s] :
             std::initializer_list<std::pair<int, const wchar_t *>>{{IDC_ACTION1, L"Добавить…"},
                                                                    {IDC_ACTION2, L"Изменить…"},
                                                                    {IDC_ACTION3, L"Вкл. / выкл."},
                                                                    {IDC_ACTION4, L"Удалить"}})
            SetDlgItemText(id, s);
        if (db_.Connected())
            rows_ = rules_.Rules();
        else
            for (const auto &rule : detector_.Rules())
                rows_.push_back({"—", rule.pattern, std::to_string(rule.weight),
                                 rule.highRisk ? "1" : "0", "1", rule.category});
        auto display = rows_;
        for (auto &a : display) {
            a[3] = Yes(a[3] == "1");
            a[4] = Yes(a[4] == "1");
        }
        SetRows(display, {{L"ID", 55},
                          {L"Шаблон", 210},
                          {L"Вес", 55},
                          {L"Высокий риск", 110},
                          {L"Включена", 88},
                          {L"Категория", 155}});
    } else if (t == 3) {
        if (db_.Connected()) {
            rows_ = scans_.TopRules();
            for (const auto &day : scans_.Days())
                chart_.days.push_back({io::Wide(day[0].substr(5)), std::stoll(day[1]), std::stoll(day[2])});
            auto counts = scans_.Totals().at(0);
            auto total = std::stoll(counts[0]), positive = std::stoll(counts[1]);
            std::ostringstream percent;
            percent << std::fixed << std::setprecision(2) << (total ? 100.0 * positive / total : 0.0);
            notice_ = "Проверок: " + counts[0] + " · Подозрительных: " + counts[1] + " (" +
                      percent.str() + "%)";
        }
        SetRows(rows_, {{L"Топ сигнатур", 280}, {L"Совпадения", 140}});
        chart_.Invalidate();
    }
}
void CSqlScanDlg::Refresh() {
    if (!ready_)
        return;
    Layout();
    const int t = Tab();
    try {
        if (t && !connection_.empty() && !db_.Connected())
            Connect();
        LoadTab(t);
    } catch (const DbError &e) {
        notice_ = e.what();
        // Only read-only views may be retried. A save may already have reached the database.
        try {
            if (!db_.Connected())
                throw;
            Connect();
            notice_.clear();
            LoadTab(t);
        } catch (const std::exception &retry) {
            UseBuiltinRules();
            page_ = 0;
            LoadTab(t);
            notice_ = "БД недоступна · " + std::string(retry.what());
        }
    } catch (const std::exception &e) {
        notice_ = e.what();
        SetRows({}, {{L"Ошибка загрузки", 300}});
    }
    for (int id = IDC_ACTION1; id <= IDC_ACTION4; ++id)
        GetDlgItem(id)->EnableWindow(db_.Connected());
    GetDlgItem(IDC_PAGE_PREV)->EnableWindow(db_.Connected() && page_ > 0);
    GetDlgItem(IDC_PAGE_NEXT)->EnableWindow(more_);
    if (t && !db_.Connected() && notice_.empty())
        notice_ = "Подключение: Приложение → Настройки базы данных";
    GetDlgItem(IDC_BTN_SAVE)->EnableWindow(report_.has_value());
    Status();
}
void CSqlScanDlg::OnTab(NMHDR *, LRESULT *result) {
    notice_.clear();
    Refresh();
    *result = 0;
}
void CSqlScanDlg::OnSort(NMHDR *hdr, LRESULT *result) {
    if (Tab() == 1) {
        int col = reinterpret_cast<NMLISTVIEW *>(hdr)->iSubItem;
        filter_.ascending = filter_.sortColumn == col ? !filter_.ascending : true;
        filter_.sortColumn = col;
        page_ = 0;
        Refresh();
    }
    *result = 0;
}
void CSqlScanDlg::OnTableOpen(NMHDR *, LRESULT *result) {
    if (Tab() == 1 || Tab() == 2)
        OnAction(IDC_ACTION2);
    *result = 0;
}
void CSqlScanDlg::ResetReport() {
    report_.reset();
    output_.SetWindowText(L"");
    GetDlgItem(IDC_BTN_SAVE)->EnableWindow(FALSE);
}
void CSqlScanDlg::OnNewInput() {
    auto input = GetDlgItem(IDC_EDIT_INPUT);
    loading_ = true;
    input->SendMessage(EM_SETREADONLY, FALSE);
    input->SetWindowText(L"");
    loading_ = false;
    previewOnly_ = false;
    OnChanged();
    Layout();
    input->SetFocus();
}
void CSqlScanDlg::OnChanged() {
    if (!ready_ || loading_)
        return;
    filename_.clear();
    fileRaw_.clear();
    fileText_.clear();
    ResetReport();
    notice_.clear();
    Status();
}
void CSqlScanDlg::OnLoad() {
    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"Текст и журналы|*.txt;*.log;*.sql;*.csv;*.tsv;*.json|Все файлы|*.*||", this);
    if (dlg.DoModal() != IDOK)
        return;
    try {
        auto raw = io::Read(dlg.GetPathName().GetString(), detector_.FileLimit());
        auto decoded = io::DecodeFile(raw);
        bool limited;
        auto view = FilePreview(io::Wide(decoded), limited);
        filename_ = io::Utf8(dlg.GetFileName().GetString());
        fileRaw_ = std::move(raw);
        fileText_ = std::move(decoded);
        previewOnly_ = limited;
        loading_ = true;
        GetDlgItem(IDC_EDIT_INPUT)->SendMessage(EM_SETREADONLY, previewOnly_);
        SetDlgItemText(IDC_EDIT_INPUT, view.c_str());
        loading_ = false;
        ResetReport();
        Layout();
        notice_ = "Файл: " + filename_ + " · " + std::to_string(fileRaw_.size()) + " байт";
        Status();
    } catch (const std::exception &e) {
        Error(e);
    }
}
void CSqlScanDlg::RenderResult() {
    auto summary = io::Wide(io::Report(*report_, "txt", true));
    // Rich Edit indexes CRLF as one character. Use CR consistently for selection offsets.
    summary.erase(std::remove(summary.begin(), summary.end(), L'\n'), summary.end());
    summary += L"\rНормализованный текст (первые 4096 байт)\r";
    auto &normalized = report_->normalizedInput;
    std::size_t limit = (std::min)(std::size_t(4096), normalized.size());
    while (limit < normalized.size() && limit &&
           (static_cast<unsigned char>(normalized[limit]) & 0xc0) == 0x80)
        --limit;
    std::string_view sample(normalized.data(), limit);
    auto snippet = io::Wide(sample);
    std::replace(snippet.begin(), snippet.end(), L'\n', L'\r');
    output_.SetWindowText((summary + snippet).c_str());
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_COLOR | CFM_BOLD;
    fmt.dwEffects = CFE_BOLD;
    CHARFORMAT2W plain = fmt;
    plain.dwEffects = 0;
    plain.crTextColor = RGB(32, 36, 40);
    output_.SendMessage(EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&plain);
    fmt.crTextColor = report_->suspicious ? RGB(152, 40, 35) : RGB(32, 97, 66);
    output_.SetSel(0, (long)summary.find(L'\r'));
    output_.SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
    const auto scoreLabel = summary.find(L"Баллы: ");
    if (scoreLabel != std::wstring::npos) {
        const auto begin = scoreLabel + 7;
        const auto length = std::to_wstring(report_->totalScore).size();
        fmt.crTextColor = ScoreColor(report_->totalScore);
        output_.SetSel(static_cast<long>(begin), static_cast<long>(begin + length));
        output_.SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    }
    fmt.crTextColor = RGB(158, 34, 32);
    long offset = static_cast<long>(summary.size());
    std::size_t cursor = 0;
    for (auto span : io::HighRiskSpans(*report_, limit)) {
        offset += static_cast<long>(io::Wide(sample.substr(cursor, span.begin - cursor)).size());
        auto end =
            offset + static_cast<long>(io::Wide(sample.substr(span.begin, span.end - span.begin)).size());
        output_.SetSel(offset, end);
        output_.SendMessage(EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
        offset = end;
        cursor = span.end;
    }
    output_.SetSel(0, 0);
    output_.SendMessage(WM_VSCROLL, SB_TOP);
    GetDlgItem(IDC_BTN_SAVE)->EnableWindow(TRUE);
}
void CSqlScanDlg::OnScan() {
    try {
        const std::string manual = filename_.empty() ? Field(*this, IDC_EDIT_INPUT) : std::string{};
        const auto &input = filename_.empty() ? manual : fileText_;
        notice_.clear();
        if (!connection_.empty()) {
            try {
                if (db_.Connected()) {
                    try {
                        detector_ = rules_.Detector();
                    } catch (const DbError &) {
                        Connect();
                    }
                } else {
                    Connect();
                }
            } catch (const std::exception &e) {
                UseBuiltinRules();
                notice_ = "БД недоступна · " + std::string(e.what());
            }
        }
        auto &raw = filename_.empty() ? input : fileRaw_;
        std::string hash = io::Hash(raw);
        if (db_.Connected() && !filename_.empty()) {
            try {
                auto last = scans_.Previous(hash);
                if (!last.empty() && AfxMessageBox(L"Этот файл уже проверялся. Открыть сохранённый отчёт?",
                                                   MB_YESNO) == IDYES) {
                    Detail(last);
                    return;
                }
            } catch (const std::exception &e) {
                notice_ = e.what();
            }
        }
        report_ = filename_.empty() ? detector_.Analyze(input) : detector_.AnalyzeLog(input);
        RenderResult();
        if (db_.Connected()) {
            try {
                auto id =
                    scans_.Save(*report_, filename_.empty() ? "manual" : "file", filename_, hash, raw.size());
                notice_ = "Сохранено в журнале · № " + id;
            } catch (const std::exception &e) {
                notice_ = "Не записано в журнал";
                Error(e);
            }
        }
        Status();
    } catch (const std::exception &e) {
        Error(e);
        Status();
    }
}
void CSqlScanDlg::OnExport() {
    if (report_)
        try {
            Export(this, L"SqlScan-report", [&](std::ostream &out, const std::string &format) {
                io::WriteReport(out, *report_, format);
            });
        } catch (const std::exception &e) {
            Error(e);
        }
}
void CSqlScanDlg::Detail(std::string id) {
    auto summary = scans_.ScanSummary(id);
    if (summary.empty())
        throw std::runtime_error("Проверка уже удалена");
    constexpr int previewLimit = 150;
    const Row &scan = summary.front();
    const auto preview = scans_.ScanHits(id, previewLimit);
    const auto lines = HasLineScores(scan) ? scans_.ScanLines(id, previewLimit) : Rows{};
    Form f(
        IDD_DETAIL, this,
        [&](Form &w) {
            const auto &a = scan;
            std::ostringstream out;
            out << "Проверка № " << a[0] << " · " << a[1] << "\r\n"
                << (a[6] == "1" ? "Подозрительно" : "Чисто") << " · " << a[7] << "\r\nИсточник: " << a[2]
                << " " << a[3] << "\r\nБаллы: " << a[4] << " · Порог: " << a[5] << "\r\nSHA-256: " << a[13]
                << "\r\nБайт входа: " << a[10] << " · Строк: " << a[8] << " · Совпадений: " << a[9]
                << " · Сохранено: " << a[14] << "\r\n";
            if (a[12] == "1")
                out << "Вход ограничен, проанализировано байт: " << a[11] << "\r\n";
            out << "\r\nПострочные оценки\r\n";
            if (HasLineScores(a)) {
                for (const auto &line : lines)
                    out << "Строка " << line[0] << " · " << line[1] << " · " << LineVerdict(line[2]) << "\r\n";
                if (std::stoull(a[8]) > lines.size())
                    out << "Остальные строки доступны в экспорте.\r\n";
            } else {
                out << missingLineScores << "\r\n";
            }
            out << "\r\nСовпадения";
            for (const auto &h : preview)
                out << "\r\n" << h[0] << ':' << h[1] << " · " << h[2] << " · +" << h[3] << " · " << h[5];
            if (std::stoull(a[14]) > preview.size())
                out << "\r\nПолный список доступен в экспорте.";
            Set(w, IDC_DETAIL, out.str());
        },
        {},
        [&](Form &w) {
            Export(&w, L"SqlScan-history", [&](std::ostream &out, const std::string &format) {
                const auto hits = scans_.ScanHits(id);
                auto allLines = HasLineScores(scan) ? scans_.ScanLines(id) : Rows{};
                if (hits.size() != std::stoull(scan[14]) ||
                    (HasLineScores(scan) && allLines.size() != std::stoull(scan[8])) ||
                    scans_.ScanSummary(id).empty())
                    throw std::runtime_error("Архивная проверка изменилась во время экспорта");
                StoredReport(out, scan, hits, std::move(allLines), format);
            });
        });
    f.DoModal();
}
void CSqlScanDlg::Rule(bool add) {
    int index = table_.GetNextItem(-1, LVNI_SELECTED);
    if (!add && index < 0)
        throw std::runtime_error("Выберите сигнатуру");
    auto cats = rules_.Categories();
    Row rule = add ? Row{"0", "", "1", "0", "1", "", cats.at(0)[0]} : rows_.at(index);
    Form form(
        IDD_RULE, this,
        [&](Form &f) {
            Set(f, IDC_PATTERN, rule[1]);
            Set(f, IDC_WEIGHT, rule[2]);
            f.CheckDlgButton(IDC_HIGH, rule[3] == "1");
            f.CheckDlgButton(IDC_ENABLED, rule[4] == "1");
            int i = 0;
            for (auto &c : cats) {
                f.Combo(IDC_CATEGORY).AddString(io::Wide(c[1]).c_str());
                if (c[0] == rule[6])
                    f.Combo(IDC_CATEGORY).SetCurSel(i);
                ++i;
            }
        },
        [&](Form &f) {
            auto w = Field(f, IDC_WEIGHT);
            if (w.size() != 1 || w[0] < '1' || w[0] > '5')
                throw std::runtime_error("Вес должен быть от 1 до 5");
            rules_.EditRule(std::stoi(rule[0]), std::stoi(cats.at(f.Combo(IDC_CATEGORY).GetCurSel())[0]),
                            Field(f, IDC_PATTERN), w[0] - '0', f.IsDlgButtonChecked(IDC_HIGH) != 0,
                            f.IsDlgButtonChecked(IDC_ENABLED) != 0);
        });
    if (form.DoModal() == IDOK)
        detector_ = rules_.Detector();
}
void CSqlScanDlg::Filters() {
    Form f(
        IDD_FILTER, this,
        [&](Form &w) {
            Set(w, IDC_SEARCH, filter_.text);
            Set(w, IDC_FROM, filter_.from);
            Set(w, IDC_UNTIL, filter_.until);
            Set(w, IDC_MIN_SCORE, filter_.minScore);
            Choice(w, IDC_VERDICT, {L"Все", L"Подозрительные", L"Чистые"},
                   filter_.verdict.empty()     ? 0
                   : filter_.verdict == "true" ? 1
                                               : 2);
            Choice(w, IDC_SOURCE, {L"Все", L"Ручной ввод", L"Файл", L"Пакет"},
                   filter_.source.empty()       ? 0
                   : filter_.source == "manual" ? 1
                   : filter_.source == "file"   ? 2
                                                : 3);
        },
        [&](Form &w) {
            auto next = filter_;
            next.text = Field(w, IDC_SEARCH);
            next.from = Field(w, IDC_FROM);
            next.until = Field(w, IDC_UNTIL);
            next.minScore = Field(w, IDC_MIN_SCORE);
            next.verdict = std::array{"", "true", "false"}.at(w.Combo(IDC_VERDICT).GetCurSel());
            next.source = std::array{"", "manual", "file", "batch"}.at(w.Combo(IDC_SOURCE).GetCurSel());
            if (!next.from.empty() && !next.until.empty() && next.from > next.until)
                throw std::runtime_error("Начало периода позже окончания");
            scans_.History(next, 0, 1);
            filter_ = next;
            page_ = 0;
        });
    f.DoModal();
}
void CSqlScanDlg::ExportHistory() {
    Export(this, L"SqlScan-journal", [&](std::ostream &out, const std::string &format) {
        if (format == "html")
            out << archiveHtml;
        Grid(out, historyHead, scans_.History(filter_, 0, INT_MAX), format, 6);
        if (format == "html")
            out << archiveEnd;
    });
}
void CSqlScanDlg::OnAction(UINT id) {
    if (!db_.Connected())
        return;
    try {
        int i = table_.GetNextItem(-1, LVNI_SELECTED);
        if (Tab() == 1) {
            if (id == IDC_ACTION1)
                Filters();
            else if (id == IDC_ACTION4)
                ExportHistory();
            else if (id == IDC_PAGE_PREV && page_ > 0)
                --page_;
            else if (id == IDC_PAGE_NEXT && more_)
                ++page_;
            else if (i >= 0 && id == IDC_ACTION2)
                Detail(rows_.at(i)[0]);
            else if (i >= 0 && id == IDC_ACTION3 &&
                     AfxMessageBox(L"Удалить выбранную проверку и её совпадения?",
                                   MB_YESNO | MB_DEFBUTTON2) == IDYES) {
                scans_.DeleteScan(rows_.at(i)[0]);
                if (rows_.size() == 1 && page_ > 0)
                    --page_;
            }
        } else if (Tab() == 2) {
            if (id == IDC_ACTION1 || id == IDC_ACTION2)
                Rule(id == IDC_ACTION1);
            else if (i >= 0) {
                auto a = rows_.at(i);
                if (id == IDC_ACTION3)
                    rules_.EditRule(std::stoi(a[0]), std::stoi(a[6]), a[1], std::stoi(a[2]), a[3] == "1",
                                    a[4] != "1");
                if (id == IDC_ACTION4 &&
                    AfxMessageBox(
                        L"Удалить сигнатуру? Правило из сохранённого отчёта можно только отключить.",
                        MB_YESNO | MB_DEFBUTTON2) == IDYES)
                    rules_.DeleteRule(std::stoi(a[0]));
                detector_ = rules_.Detector();
            }
        }
        Refresh();
    } catch (const std::exception &e) {
        Error(e);
        Refresh();
    }
}
