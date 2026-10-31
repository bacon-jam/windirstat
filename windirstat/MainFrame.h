// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "PacMan.h"
#include "FileTabbedView.h"
#include "Dialogs/ProgressDlg.h"
#include "LayoutPopup.h"

class CWdsSplitterWnd;
class CMainFrame;

class CFileTreeView;
class CExtensionView;
class CVisualizationPane;

//
// The "logical focus" can be
// - on the Directory List
// - on the Extension List
// Although these windows can lose the real focus, for instance
// when a dialog box is opened, the logical focus will not be lost.
//
enum LOGICAL_FOCUS : uint8_t
{
    LF_NONE             = 0,
    LF_FILETREE         = 1 << 0,
    LF_DUPELIST         = 1 << 1,
    LF_TOPLIST          = 1 << 2,
    LF_SEARCHLIST       = 1 << 3,
    LF_WATCHERLIST      = 1 << 4,
    LF_PERMSLIST        = 1 << 5,
    LF_EXTLIST          = 1 << 6,
    LF_STORAGEANALYTICS = 1 << 7,
};

//
// CSettingsSheet.
//
class CSettingsSheet final : public MessageTarget<CSettingsSheet, CPropertySheet>
{
public:
    CSettingsSheet();
    void SetRestartRequired(const int darkMode, const int language)
    {
        m_restartRequest = darkMode != m_initialDarkMode || language != m_initialLanguage;
    }
    bool OnInitDialog() override;
    static bool ShowSettings(int initialPage = -1, bool refreshOnFilteringChange = true);

    bool m_restartApplication = false; // [out]
    int m_initialPage = -1;

protected:
    bool OnCommand(WPARAM wParam, LPARAM lParam) override;
    bool OnEraseBkgnd(CDC* pDC) const;
    bool ConfirmApply(UINT command) override;
    void OnApplied(UINT command) override;

    const int m_initialDarkMode = COptions::DarkMode;
    const int m_initialLanguage = COptions::LanguageId;
    bool m_restartAfterApply = false;
    bool m_restartRequest = false;
    bool m_alreadyAsked = false;

public:
    static std::span<const RouteEntry> Routes();

};

//
// CWdsSplitterWnd. A CSplitterWnd with 2 columns or rows, which
// knows about the current split ratio and retains it even when resized.
//
class CWdsSplitterWnd final : public MessageTarget<CWdsSplitterWnd, CSplitterWnd>
{
public:
    CWdsSplitterWnd(double * splitterPos);
    void StopTracking(bool bAccept) override;
    void SetSplitterPos(double pos);
    void RestoreSplitterPos(double posIfVirgin);
    void ResetUserPosition() { m_wasTrackedByUser = false; }
    void SetStorage(double* ptr) { m_userSplitterPos = ptr; m_wasTrackedByUser = (*ptr > 0.0 && *ptr < 1.0); }
    void ClearPaneTracking();
    void TrackPane(int pane, std::function<void(bool)> onToggle, std::function<void()> onMinimize);

protected:
    bool PreCreateWindow(CREATESTRUCT& cs) override;
    void OnTrackingChanged(bool tracking) override;

    struct PaneTracking
    {
        std::function<void(bool)> onToggle;
        std::function<void()> onMinimize;
    };

    double m_splitterPos{0};    // Current split ratio
    bool m_wasTrackedByUser;    // True as soon as user has modified the splitter position
    double * m_userSplitterPos; // Split ratio as set by the user
    PaneTracking m_paneTracking[2];

public:
    static std::span<const RouteEntry> Routes();

protected:
    void OnSize(UINT nType, int cx, int cy);
};

//
// CPacmanControl. Pacman on the status bar.
//
class CPacmanControl final : public MessageTarget<CPacmanControl, CWnd>
{
public:
    CPacmanControl() = default;
    void Drive();
    void Start();
    void Stop();

protected:
    CPacman m_pacman;

public:
    static std::span<const RouteEntry> Routes();

protected:
    void OnPaint();
    int OnCreate(LPCREATESTRUCT lpCreateStruct);
    bool OnEraseBkgnd(CDC* pDC);
};

class CWatcherFilterEdit final : public MessageTarget<CWatcherFilterEdit, CEdit>
{
public:
    bool PreprocessMessage(MSG* message) override;
    static std::span<const RouteEntry> Routes();

private:
    void OnChange();
    void OnPaint();
    bool m_valid = true;
};

//
// CMainFrame. The main application window.
//
class CMainFrame final : public MessageTarget<CMainFrame, CFrameWnd>
{
protected:
    static constexpr DWORD WM_CALLBACKUI = WM_APP + 1;
    static UINT s_TaskBarMessage;
    inline static CMainFrame* s_Singleton = nullptr;

public:
    CMainFrame();
    ~CMainFrame() override;

    void InitialShowWindow();
    void InvokeInMessageThread(std::function<void()> callback) const;

    void RestoreVisualizationPane(bool force = false);
    void ExpandFileTabbedView();
    void RestoreSplitterPositions();
    void ApplyPaneVisibility(bool restoreDuringScan = false);

    // The main panes; the splitters only arrange them
    std::unique_ptr<CFileTabbedView> m_fileTabbedView;
    std::unique_ptr<CExtensionView> m_extensionView;
    std::unique_ptr<CVisualizationPane> m_visualizationPane;
    CFileTreeView* GetFileTreeView() const { return m_fileTabbedView->GetFileTreeView(); }
    CFileTopView* GetFileTopView() const { return m_fileTabbedView->GetFileTopView(); }
    CFileDupeView* GetFileDupeView() const { return m_fileTabbedView->GetFileDupeView(); }
    CFileSearchView* GetFileSearchView() const { return m_fileTabbedView->GetFileSearchView(); }
    CFileWatcherView* GetFileWatcherView() const { return m_fileTabbedView->GetFileWatcherView(); }
    CFilePermsView* GetFilePermsView() const { return m_fileTabbedView->GetFilePermsView(); }
    CFileTabbedView* GetFileTabbedView() const { return m_fileTabbedView.get(); }
    CExtensionView* GetExtensionView() const { return m_extensionView.get(); }
    GraphPane GetGraphPaneType() const;
    void SelectGraphPane(GraphPane pane);
    void ShowVisualization(bool show) const;
    bool IsVisualizationShown() const;
    CWinDirStatPane* GetVisualizationPane() const;
    CWinDirStatPane* GetActiveVisualization() const;

    void CreateProgress(ULONGLONG range);
    void UpdateProgressRange(ULONGLONG range);
    void SetProgressComplete();
    void SuspendState(bool suspend);
    bool IsScanSuspended() const { return m_scanSuspend; }

    void UpdateProgress();
    void UpdateDynamicMenuItems(MenuRef menu, MenuRef menuHeader = nullptr) const;
    std::pair<MenuRef, int> LocateNamedMenu(MenuRef menu, const std::wstring& subMenuText, bool removeItems = true) const;

    void SetLogicalFocus(LOGICAL_FOCUS lf);
    LOGICAL_FOCUS GetLogicalFocus() const { return m_logicalFocus; }
    void MoveFocus(LOGICAL_FOCUS logicalFocus);
    void UpdatePaneText();

    static void QueryRecycleBin(ULONGLONG& items, ULONGLONG& bytes);

    bool OnCreateClient() override;
    bool PreCreateWindow(CREATESTRUCT& cs) override;

    void CreateStatusProgress();
    void CreatePacmanProgress();
    void LayoutProgress();
    void DestroyProgress();

    void SetStatusPaneText(const CDC& cdc, CStatusBar::PaneId pane, const std::wstring& text, int minWidth = 0);
    void UpdateCleanupMenu(MenuRef menu, bool triggerAsync = true);

    UINT_PTR m_timer = 0;           // Timer for updating the display
    bool m_progressVisible = false; // True while progress must be shown (either pacman or progress bar)
    bool m_scanSuspend = false;     // True if the scan has been suspended
    bool m_placementRestored = false;
    bool m_fontChangePending = false;
    bool m_shuttingDown = false;    // Marks the process is shutting down so we can exit timers
    ULONGLONG m_progressRange = 0;  // Progress range. A range of 0 means Pacman should be used.
    ULONGLONG m_progressPos = 0;    // Progress position (<= progressRange, or an item count when m_progressRange == 0)
    CItem* m_workingItem = nullptr;

    CWdsSplitterWnd m_subSplitter{ COptions::SubSplitterPos.Ptr() }; // Nested splitter holding two of the panes
    CWdsSplitterWnd m_splitter{ COptions::MainSplitterPos.Ptr() };    // Holds the nested splitter and the remaining pane
    CLayoutPopup m_layoutPopup;                                        // Floating layout-picker popup

    CStatusBar m_wndStatusBar; // Status bar
    CToolBar m_wndToolBar;     // Toolbar
    CWatcherFilterEdit m_watcherFilter;
    CSize m_defaultButtonSize;  // Toolbar button size at creation, before DPI and size scaling
    int m_watcherAutoScrollOnImage = -1;
    int m_watcherAutoScrollOffImage = -1;
    CWdsProgressCtrl m_progress;  // Progress control. Is Create()ed and Destroy()ed again every time.
    CPacmanControl m_pacman;      // Static control for Pacman
    LOGICAL_FOCUS m_logicalFocus = LF_NONE; // Which view has the logical focus

    CComPtr<ITaskbarList3> m_taskbarList;
    TBPFLAG m_taskbarButtonState = TBPF_INDETERMINATE;
    TBPFLAG m_taskbarButtonPreviousState = TBPF_INDETERMINATE;

    // Cached values for cleanup menu queries (updated asynchronously)
    ULONGLONG m_recycleBinItems = 0;
    ULONGLONG m_recycleBinBytes = 0;
    ULONGLONG m_shadowCopyCount = 0;
    ULONGLONG m_shadowCopyBytes = 0;
    std::future<std::array<ULONGLONG, 4>> m_cleanupQuery;
    std::jthread m_cleanupThread;

static std::span<const RouteEntry> Routes();

protected:
    void PostNcDestroy() override;
    void SavePersistentAttributes() const override;
    void OnEndSession(bool ending) const;
    CCmdTarget* GetCommandTarget() const override { return CWinDirStatModel::Get(); }
    int OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnSetFocus(WindowRef pOldWnd);
    void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    LRESULT OnEnterSizeMove(WPARAM, LPARAM) const;
    LRESULT OnExitSizeMove(WPARAM, LPARAM) const;
    LRESULT OnCallbackRequest(WPARAM, LPARAM lParam);
    void OnTimer(UINT_PTR nIDEvent);
    void OnClose();
    void OnInitMenuPopup(MenuRef pPopupMenu, UINT nIndex, bool bSysMenu);
    LRESULT OnMenuCommand(WPARAM position, LPARAM menuHandle);
    void OnSize(UINT nType, int cx, int cy);
    void OnUpdateViewShowVisualization(CCmdUI* pCmdUI) const;
    void OnUpdateTreeMapUseLogical(CCmdUI* pCmdUI);
    void OnUpdateTreeMapUsePhysical(CCmdUI* pCmdUI);
    void OnUpdateViewAbsolutePercentages(CCmdUI* pCmdUI);
    void OnUpdateViewShowFileTypes(CCmdUI* pCmdUI) const;
    void OnUpdateViewGroupUnregisteredTypes(CCmdUI* pCmdUI) const;
    void OnUpdateViewShowWatcher(CCmdUI* pCmdUI) const;
    void OnViewShowVisualization();
    void OnViewTreeMapStyle(UINT commandId);
    void OnUpdateViewTreeMapStyle(CCmdUI* pCmdUI) const;
    void OnViewGraphPreset(UINT commandId);
    void OnUpdateViewGraphPreset(CCmdUI* pCmdUI) const;
    void OnViewFlameGraph();
    void OnUpdateViewFlameGraph(CCmdUI* pCmdUI) const;
    void OnViewSunburst();
    void OnUpdateViewSunburst(CCmdUI* pCmdUI) const;
    void OnViewTreeMapUseLogical();
    void OnViewTreeMapUsePhysical();
    void OnViewAbsolutePercentages() const;
    void OnViewShowFileTypes();
    void OnViewGroupUnregisteredTypes() const;
    void OnViewShowExtensionsOnTreeMap() const;
    void OnUpdateViewShowExtensionsOnTreeMap(CCmdUI* pCmdUI) const;
    void OnViewShowFolderFramesOnTreeMap() const;
    void OnUpdateViewShowFolderFramesOnTreeMap(CCmdUI* pCmdUI) const;
    void OnViewAllFiles() const { GetFileTabbedView()->SetActiveFileTreeView(); }
    void OnViewLargestFiles() const { GetFileTabbedView()->SetActiveTopView(); }
    void OnViewDuplicateFiles() const { GetFileTabbedView()->SetActiveDupeView(); }
    void OnViewSearchResults() const { GetFileTabbedView()->SetActiveSearchView(); }
    void OnViewToolBarSize(UINT commandId);
    void OnUpdateViewToolBarSize(CCmdUI* pCmdUI) const;
    void OnViewFontSize(UINT commandId);
    void OnUpdateViewFontSize(CCmdUI* pCmdUI) const;
    void OnAdvancedShadowCopy(UINT nID);
    void OnAdvancedDefrag(UINT nID);
    void OnAdvancedChkdsk(UINT nID);
    void OnToolsWatcher() const;
    void OnWatcherStart();
    void OnUpdateWatcherStart(CCmdUI* pCmdUI);
    void OnWatcherPause();
    void OnUpdateWatcherPause(CCmdUI* pCmdUI);
    void OnWatcherAutoScroll();
    void OnUpdateWatcherAutoScroll(CCmdUI* pCmdUI);
    void OnWatcherClear();
    void OnUpdateWatcherClear(CCmdUI* pCmdUI);
    void OnToolsRecovery();
    void OnUpdateToolsRecovery(CCmdUI* pCmdUI) const;
    void OnToolsPermissions() const;
    void OnUpdateToolsPermissions(CCmdUI* pCmdUI) const;
    void OnToolsStorageAnalytics() const;
    void OnUpdateToolsStorageAnalytics(CCmdUI* pCmdUI) const;
    void UpdateToolsMenu(MenuRef menu) const;
    void OnViewWindowLayout();
    void OnConfigure();
    void OnDestroy();
    LRESULT OnTaskButtonCreated(WPARAM, LPARAM);
    UINT OnPowerBroadcast(UINT, LPARAM);
    void OnSysColorChange();
    void OnAppearanceChanged() override;
    void OnSettingChange(UINT, LPCTSTR);
    LRESULT OnUahDrawMenu(WPARAM wParam, LPARAM lParam) const;
    void OnNcPaint();
    bool OnNcActivate(bool bActive);
    bool OnEraseBkgnd(CDC* dc);
public:
    static CMainFrame* Get() { return s_Singleton; }
    static std::wstring GetFullBaseTitle();
    void UpdateFrameTitleForScan(std::wstring_view scanName = {});
    void UpdateAllPanes(CWnd* sender, MODEL_CHANGE change, CItem* item) const;
    void RebuildToolBar(bool rebuildButtons = true);
    void SetWatcherToolBarButtons(bool visible, bool updateLayout = true);
    void RebuildLayout(bool resetPositions = false);
    bool CreateFromResource(UINT nIDResource) override;

private:
    void LayoutWatcherFilter();
    void OnFontSizeChanged(int oldPercent, int newPercent) override;
    void BuildSplitterLayout();
};

inline std::span<const RouteEntry> CSettingsSheet::Routes()
{
    static constexpr std::array entries
    {
        Route::Window<&CSettingsSheet::OnEraseBkgnd>(WM_ERASEBKGND),
    };
    return entries;
}

inline std::span<const RouteEntry> CWdsSplitterWnd::Routes()
{
    static constexpr std::array entries
    {
        Route::Window<&CWdsSplitterWnd::OnSize>(WM_SIZE),
    };
    return entries;
}

inline std::span<const RouteEntry> CPacmanControl::Routes()
{
    static constexpr std::array entries
    {
        Route::Window<&CPacmanControl::OnPaint>(WM_PAINT),
        Route::Window<&CPacmanControl::OnCreate>(WM_CREATE),
        Route::Window<&CPacmanControl::OnEraseBkgnd>(WM_ERASEBKGND),
    };
    return entries;
}

inline std::span<const RouteEntry> CWatcherFilterEdit::Routes()
{
    static constexpr std::array entries
    {
        Route::ReflectControl<&CWatcherFilterEdit::OnChange>(EN_CHANGE),
        Route::Window<&CWatcherFilterEdit::OnPaint>(WM_NCPAINT),
        Route::Window<&CWatcherFilterEdit::OnPaint>(WM_PAINT),
        Route::Window<&CWatcherFilterEdit::OnPaint>(WM_PRINT),
    };
    return entries;
}

inline std::span<const RouteEntry> CMainFrame::Routes()
{
    static constexpr std::array entries
    {
        Route::Command<&CMainFrame::OnConfigure>(ID_CONFIGURE),
        Route::Command<&CMainFrame::OnViewShowFileTypes>(ID_VIEW_SHOWFILETYPES),
        Route::Command<&CMainFrame::OnViewGroupUnregisteredTypes>(ID_VIEW_GROUP_TYPES),
        Route::Command<&CMainFrame::OnViewShowVisualization>(ID_VIEW_SHOWVISUALIZATION),
        Route::Command<&CMainFrame::OnViewTreeMapStyle>(ID_VIEW_TREEMAP_ROWS, ID_VIEW_TREEMAP_MOORE),
        Route::Command<&CMainFrame::OnViewGraphPreset>(ID_VIEW_GRAPH_PRESET_CLASSIC, ID_VIEW_GRAPH_PRESET_CUSTOM),
        Route::Command<&CMainFrame::OnViewFlameGraph>(ID_VIEW_FLAMEGRAPH),
        Route::Command<&CMainFrame::OnViewSunburst>(ID_VIEW_SUNBURST),
        Route::Command<&CMainFrame::OnViewTreeMapUseLogical>(ID_TREEMAP_LOGICAL_SIZE),
        Route::Command<&CMainFrame::OnViewTreeMapUsePhysical>(ID_TREEMAP_PHYSICAL_SIZE),
        Route::Command<&CMainFrame::OnViewAbsolutePercentages>(ID_VIEW_ABSOLUTE_PERCENTAGES),
        Route::Window<&CMainFrame::OnEnterSizeMove>(WM_ENTERSIZEMOVE),
        Route::Window<&CMainFrame::OnExitSizeMove>(WM_EXITSIZEMOVE),
        Route::Window<&CMainFrame::OnCallbackRequest>(WM_CALLBACKUI),
        Route::Window<&CMainFrame::OnUahDrawMenu>(DarkMode::WM_UAHDRAWMENU),
        Route::Window<&CMainFrame::OnUahDrawMenu>(DarkMode::WM_UAHDRAWMENUITEM),
        Route::Registered<&CMainFrame::OnTaskButtonCreated>(s_TaskBarMessage),
        Route::Update<&CMainFrame::OnUpdateViewShowVisualization>(ID_VIEW_SHOWVISUALIZATION),
        Route::Update<&CMainFrame::OnUpdateViewTreeMapStyle>(ID_VIEW_TREEMAP_ROWS, ID_VIEW_TREEMAP_MOORE),
        Route::Update<&CMainFrame::OnUpdateViewGraphPreset>(ID_VIEW_GRAPH_PRESET_CLASSIC, ID_VIEW_GRAPH_PRESET_CUSTOM),
        Route::Update<&CMainFrame::OnUpdateViewFlameGraph>(ID_VIEW_FLAMEGRAPH),
        Route::Update<&CMainFrame::OnUpdateViewSunburst>(ID_VIEW_SUNBURST),
        Route::Update<&CMainFrame::OnUpdateViewShowFileTypes>(ID_VIEW_SHOWFILETYPES),
        Route::Update<&CMainFrame::OnUpdateViewGroupUnregisteredTypes>(ID_VIEW_GROUP_TYPES),
        Route::Update<&CMainFrame::OnUpdateTreeMapUseLogical>(ID_TREEMAP_LOGICAL_SIZE),
        Route::Update<&CMainFrame::OnUpdateTreeMapUsePhysical>(ID_TREEMAP_PHYSICAL_SIZE),
        Route::Update<&CMainFrame::OnUpdateViewAbsolutePercentages>(ID_VIEW_ABSOLUTE_PERCENTAGES),
        Route::Command<&CMainFrame::OnViewShowExtensionsOnTreeMap>(ID_TREEMAP_SHOW_EXTENSIONS),
        Route::Update<&CMainFrame::OnUpdateViewShowExtensionsOnTreeMap>(ID_TREEMAP_SHOW_EXTENSIONS),
        Route::Command<&CMainFrame::OnViewShowFolderFramesOnTreeMap>(ID_TREEMAP_SHOW_FOLDER_FRAMES),
        Route::Update<&CMainFrame::OnUpdateViewShowFolderFramesOnTreeMap>(ID_TREEMAP_SHOW_FOLDER_FRAMES),
        Route::Update<&CMainFrame::OnUpdateViewShowWatcher>(ID_TOOLS_WATCHER),
        Route::Window<&CMainFrame::OnClose>(WM_CLOSE),
        Route::Window<&CMainFrame::OnEndSession>(WM_ENDSESSION),
        Route::Window<&CMainFrame::OnCreate>(WM_CREATE),
        Route::Window<&CMainFrame::OnDestroy>(WM_DESTROY),
        Route::Window<&CMainFrame::OnInitMenuPopup>(WM_INITMENUPOPUP),
        Route::Window<&CMainFrame::OnMenuCommand>(WM_MENUCOMMAND),
        Route::Window<&CMainFrame::OnSize>(WM_SIZE),
        Route::Window<&CMainFrame::OnSysColorChange>(WM_SYSCOLORCHANGE),
        Route::Window<&CMainFrame::OnSettingChange>(WM_SETTINGCHANGE),
        Route::Window<&CMainFrame::OnPowerBroadcast>(WM_POWERBROADCAST),
        Route::Window<&CMainFrame::OnTimer>(WM_TIMER),
        Route::Window<&CMainFrame::OnNcPaint>(WM_NCPAINT),
        Route::Window<&CMainFrame::OnNcActivate>(WM_NCACTIVATE),
        Route::Window<&CMainFrame::OnEraseBkgnd>(WM_ERASEBKGND),
        Route::Window<&CMainFrame::OnSetFocus>(WM_SETFOCUS),
        Route::Window<&CMainFrame::OnKeyDown>(WM_KEYDOWN),
        Route::Command<&CMainFrame::OnViewAllFiles>(ID_VIEW_ALL_FILES),
        Route::Command<&CMainFrame::OnViewLargestFiles>(ID_VIEW_LARGEST_FILES),
        Route::Command<&CMainFrame::OnViewDuplicateFiles>(ID_VIEW_DUPLICATE_FILES),
        Route::Command<&CMainFrame::OnViewSearchResults>(ID_VIEW_SEARCH_RESULTS),
        Route::Command<&CMainFrame::OnViewToolBarSize>(ID_VIEW_TOOLBAR_SIZE_100, ID_VIEW_TOOLBAR_SIZE_USE_WINDOWS),
        Route::Update<&CMainFrame::OnUpdateViewToolBarSize>(ID_VIEW_TOOLBAR_SIZE_100, ID_VIEW_TOOLBAR_SIZE_USE_WINDOWS),
        Route::Command<&CMainFrame::OnViewFontSize>(ID_VIEW_FONT_SIZE_100, ID_VIEW_FONT_SIZE_USE_WINDOWS),
        Route::Update<&CMainFrame::OnUpdateViewFontSize>(ID_VIEW_FONT_SIZE_100, ID_VIEW_FONT_SIZE_USE_WINDOWS),
        Route::Command<&CMainFrame::OnAdvancedShadowCopy>(ID_TOOLS_SHADOW_COPY_BASE,
            ID_TOOLS_SHADOW_COPY_BASE + wds::alphaSize - 1),
        Route::Command<&CMainFrame::OnAdvancedDefrag>(ID_TOOLS_DEFRAG_BASE, ID_TOOLS_DEFRAG_BASE + wds::alphaSize - 1),
        Route::Command<&CMainFrame::OnAdvancedChkdsk>(ID_TOOLS_CHKDSK_BASE, ID_TOOLS_CHKDSK_BASE + wds::alphaSize - 1),
        Route::Command<&CMainFrame::OnToolsWatcher>(ID_TOOLS_WATCHER),
        Route::Command<&CMainFrame::OnWatcherStart>(ID_WATCHER_START),
        Route::Update<&CMainFrame::OnUpdateWatcherStart>(ID_WATCHER_START),
        Route::Command<&CMainFrame::OnWatcherPause>(ID_WATCHER_PAUSE),
        Route::Update<&CMainFrame::OnUpdateWatcherPause>(ID_WATCHER_PAUSE),
        Route::Command<&CMainFrame::OnWatcherAutoScroll>(ID_WATCHER_AUTOSCROLL),
        Route::Update<&CMainFrame::OnUpdateWatcherAutoScroll>(ID_WATCHER_AUTOSCROLL),
        Route::Command<&CMainFrame::OnWatcherClear>(ID_WATCHER_CLEAR),
        Route::Update<&CMainFrame::OnUpdateWatcherClear>(ID_WATCHER_CLEAR),
        Route::Command<&CMainFrame::OnToolsRecovery>(ID_TOOLS_RECOVERY),
        Route::Update<&CMainFrame::OnUpdateToolsRecovery>(ID_TOOLS_RECOVERY),
        Route::Command<&CMainFrame::OnToolsPermissions>(ID_TOOLS_PERMISSIONS),
        Route::Update<&CMainFrame::OnUpdateToolsPermissions>(ID_TOOLS_PERMISSIONS),
        Route::Command<&CMainFrame::OnToolsStorageAnalytics>(ID_TOOLS_STORAGE_ANALYTICS),
        Route::Update<&CMainFrame::OnUpdateToolsStorageAnalytics>(ID_TOOLS_STORAGE_ANALYTICS),
        Route::Command<&CMainFrame::OnViewWindowLayout>(ID_VIEW_WINDOW_LAYOUT),
    };
    return entries;
}
