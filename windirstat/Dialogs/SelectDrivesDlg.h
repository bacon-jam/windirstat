// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

#include "pch.h"
#include "WdsListControl.h"
#include "Layout.h"

inline constexpr UINT WM_WDS_SELECT_DRIVES_OK = WM_APP + 0x110;

//
// The dialog has these three radio buttons.
//
enum RADIO : std::uint8_t
{
    RADIO_TARGET_DRIVES_ALL,
    RADIO_TARGET_DRIVES_SUBSET,
    RADIO_TARGET_FOLDER
};

class CDrivesList;

//
// CDriveItem. An item in the CDrivesList Control.
// All methods are called by the gui thread.
//
class CDriveItem final : public CWdsListItem
{
public:
    CDriveItem(CDrivesList* list, const std::wstring& pszPath, std::wstring name = {});

    void StartQuery();
    bool UpdateDriveInformation();
    bool IsQuerying() const { return m_query.valid(); }

    int Compare(const CWdsListItem* baseOther, int subitem) const override;

    std::wstring GetPath() const { return m_path; }
    std::wstring GetDrive() const;
    bool IsRemote() const { return m_isRemote; }
    bool IsMtp() const { return m_mtp; }
    bool IsSUBSTed() const { return m_subst; }
    bool DrawSubItem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) override;
    std::wstring GetText(int subitem) const override;
    HICON GetIcon() override { return m_icon; }

private:
    CDrivesList* m_driveList; // Backpointer
    std::wstring m_path; // e.g. "C:\"
    bool m_mtp = false;
    HICON m_icon = nullptr; // Cached icon
    bool m_isRemote; // Whether the drive type is DRIVE_REMOTE (network drive)

    bool m_success = false; // Drive is accessible. false while querying.
    bool m_subst = false; // Drive is subst'd

    std::wstring m_name; // e.g. "BOOT (C:)"
    ULONGLONG m_totalBytes = 0; // Capacity
    ULONGLONG m_freeBytes = 0;  // Free space

    double m_used = 0.0; // used space / total space

    struct DriveInformation
    {
        bool success = false;
        std::wstring name;
        ULONGLONG total = 0;
        ULONGLONG free = 0;
    };

    std::shared_future<DriveInformation> m_query;
};

//
// CDrivesList.
//
class CDrivesList final : public MessageTarget<CDrivesList, CWdsListControl>
{
    friend class CSelectDrivesDlg;
public:
    CDrivesList();
    CDriveItem* GetItem(const int i) const { return reinterpret_cast<CDriveItem*>(CWdsListControl::GetItem(i)); }
    void SelectItem(const CDriveItem* item);
    bool IsItemSelected(const int i) const { return LVIS_SELECTED == GetItemState(i, LVIS_SELECTED); }
    bool IsSorting() const { return m_sortInProgress; }
    void SortItems() override;

static std::span<const RouteEntry> Routes();

protected:
    void OnLvnDeleteItem(NMHDR* pNMHDR, LRESULT* pResult) const;
    void OnDoubleClick(NMHDR* pNMHDR, LRESULT* pResult);

private:
    bool m_sortInProgress = false;
};

//
// CSelectDrivesDlg. The initial dialog, where the user can select
// one or more drives or folders for scanning.
//
class CSelectDrivesDlg final : public MessageTarget<CSelectDrivesDlg, CLayoutDialog>
{
public:
    enum : std::uint8_t { IDD = IDD_SELECTDRIVES };
    static constexpr UINT_PTR QUERY_TIMER_ID = 1;

    CSelectDrivesDlg(CWnd* pParent = nullptr);

    std::vector<std::wstring> GetSelectedItems() const;
    bool OnInitDialog() override;
    void OnOK() override;
    void UpdateButtons(const std::wstring* folderOverride = nullptr);
    void UpdateFilterButton();
    void SetActiveRadio(int radio);

protected:

    // Dialog Data
    int m_radio = 0;          // out.
    std::wstring m_folderName;    // out. Valid if m_radio = RADIO_TARGET_FOLDER
    std::vector<std::wstring> m_drives;    // out. Valid if m_radio != RADIO_TARGET_FOLDER
    CDrivesList m_driveList;
    CComboBox m_browseList;
    CButton m_okButton;
    CStatic m_browseButton;
    CStatic m_addFolderButton;
    CStatic m_filterButton;
    SmartPointer<HICON, decltype(&DestroyIcon)> m_browseIcon{ &DestroyIcon };
    SmartPointer<HICON, decltype(&DestroyIcon)> m_addFolderIcon{ &DestroyIcon };
    SmartPointer<HICON, decltype(&DestroyIcon)> m_filterIcon{ &DestroyIcon };
    std::vector<std::wstring> m_selectedDrives;
    bool m_suppressItemChanged = false;

public:
    static std::span<const RouteEntry> Routes();

protected:
    bool SaveSelection();
    void OnBnClickedUpdateButtons();
    void OnBnClickedFastScanCheckbox();
    void OnLvnItemChangedDrives(NMHDR* pNMHDR, LRESULT* pResult);
    LRESULT OnWmuOk(WPARAM, LPARAM);
    void OnTimer(UINT_PTR nIDEvent);
    void OnSysColorChange();
    void OnBnClickedRadioTargetDrivesAll();
    void OnBnClickedRadioTargetDrivesSubset();
    void OnBnClickedRadioTargetFolder();
    void OnBnDoubleclickedRadio();
    void OnNMSetfocusTargetDrivesList(NMHDR*, LRESULT* pResult);
    void BrowseFolders(bool append);
    void OnBnClickedBrowseButton() { BrowseFolders(false); }
    void OnBnClickedAddFolder() { BrowseFolders(true); }
    void OnBnClickedFilterButton();
    bool PreprocessMessage(MSG* pMsg) override;
    void OnEditchangeBrowseFolder();
    void OnSelchangeBrowseFolder();
};

// Recent cloud connections and their editor keep local scan preferences independent.
class CSelectCloudDlg final : public MessageTarget<CSelectCloudDlg, CLayoutDialog>
{
public:
    CSelectCloudDlg(CWnd* parent = nullptr);

    const std::wstring& GetSelectedPath() const { return m_path; }
    bool OnInitDialog() override;
    void OnOK() override;
    static std::span<const RouteEntry> Routes();

protected:
    void OnNew();
    void OnEdit();
    void OnRemove();
    void OnSelectionChanged(NMHDR*, LRESULT* result);
    void OnDoubleClick(NMHDR*, LRESULT* result);
    void OnListKeyDown(NMHDR* header, LRESULT* result);
    void OnDisplayInfo(NMHDR* header, LRESULT* result);
    void OnSize(UINT type, int width, int height);
    void OnDestroy();
    void SavePersistentAttributes() const override;

private:
    class RecentList final : public CListCtrl
    {
        MessageResult OnNotify(WPARAM wParam, LPARAM lParam) override;
    } m_recent;
    bool ShowEditor(int index);
    void ReloadRecentTargets();
    void UpdateButtons();
    std::vector<std::wstring> m_recentTargets;
    std::wstring m_path, m_cell;
    CFont m_headingFont;
    bool m_initialized = false;
};

inline std::span<const RouteEntry> CDrivesList::Routes()
{
    static constexpr std::array entries
    {
        Route::ReflectNotify<&CDrivesList::OnLvnDeleteItem>(LVN_DELETEITEM),
        Route::ReflectNotify<&CDrivesList::OnDoubleClick>(NM_DBLCLK),
    };
    return entries;
}

inline std::span<const RouteEntry> CSelectDrivesDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Control<&CSelectDrivesDlg::OnBnClickedBrowseButton>(STN_CLICKED, IDC_BROWSE_BUTTON),
        Route::Control<&CSelectDrivesDlg::OnBnClickedAddFolder>(STN_CLICKED, IDC_ADD_FOLDER),
        Route::Control<&CSelectDrivesDlg::OnBnClickedFilterButton>(STN_CLICKED, IDC_FILTER_BUTTON),
        Route::Control<&CSelectDrivesDlg::OnBnClickedFastScanCheckbox>(BN_CLICKED, IDC_FAST_SCAN_CHECKBOX),
        Route::Control<&CSelectDrivesDlg::OnBnClickedRadioTargetDrivesAll>(BN_CLICKED, IDC_RADIO_TARGET_DRIVES_ALL),
        Route::Control<&CSelectDrivesDlg::OnBnClickedRadioTargetDrivesSubset>(BN_CLICKED, IDC_RADIO_TARGET_DRIVES_SUBSET),
        Route::Control<&CSelectDrivesDlg::OnBnClickedRadioTargetFolder>(BN_CLICKED, IDC_RADIO_TARGET_FOLDER),
        Route::Control<&CSelectDrivesDlg::OnBnClickedUpdateButtons>(BN_CLICKED, IDC_SCAN_DUPLICATES),
        Route::Control<&CSelectDrivesDlg::OnBnDoubleclickedRadio>(BN_DOUBLECLICKED, IDC_RADIO_TARGET_DRIVES_ALL),
        Route::Control<&CSelectDrivesDlg::OnBnDoubleclickedRadio>(BN_DOUBLECLICKED, IDC_RADIO_TARGET_DRIVES_SUBSET),
        Route::Control<&CSelectDrivesDlg::OnBnDoubleclickedRadio>(BN_DOUBLECLICKED, IDC_RADIO_TARGET_FOLDER),
        Route::Control<&CSelectDrivesDlg::OnEditchangeBrowseFolder>(CBN_EDITCHANGE, IDC_BROWSE_FOLDER),
        Route::Control<&CSelectDrivesDlg::OnSelchangeBrowseFolder>(CBN_SELCHANGE, IDC_BROWSE_FOLDER),
        Route::Notify<&CSelectDrivesDlg::OnLvnItemChangedDrives>(LVN_ITEMCHANGED, IDC_TARGET_DRIVES_LIST),
        Route::Notify<&CSelectDrivesDlg::OnNMSetfocusTargetDrivesList>(NM_SETFOCUS, IDC_TARGET_DRIVES_LIST),
        Route::Window<&CSelectDrivesDlg::OnWmuOk>(WM_WDS_SELECT_DRIVES_OK),
        Route::Window<&CSelectDrivesDlg::OnTimer>(WM_TIMER),
        Route::Window<&CSelectDrivesDlg::OnSysColorChange>(WM_SYSCOLORCHANGE),
    };
    return entries;
}

inline std::span<const RouteEntry> CSelectCloudDlg::Routes()
{
    static constexpr std::array entries
    {
        Route::Control<&CSelectCloudDlg::OnNew>(BN_CLICKED, IDC_REMOTE_NEW),
        Route::Control<&CSelectCloudDlg::OnEdit>(BN_CLICKED, IDC_REMOTE_EDIT),
        Route::Control<&CSelectCloudDlg::OnRemove>(BN_CLICKED, IDC_REMOTE_REMOVE),
        Route::Notify<&CSelectCloudDlg::OnSelectionChanged>(LVN_ITEMCHANGED, IDC_REMOTE_RECENT),
        Route::Notify<&CSelectCloudDlg::OnSelectionChanged>(LVN_ODSTATECHANGED, IDC_REMOTE_RECENT),
        Route::Notify<&CSelectCloudDlg::OnDoubleClick>(NM_DBLCLK, IDC_REMOTE_RECENT),
        Route::Notify<&CSelectCloudDlg::OnListKeyDown>(LVN_KEYDOWN, IDC_REMOTE_RECENT),
        Route::Notify<&CSelectCloudDlg::OnDisplayInfo>(LVN_GETDISPINFO, IDC_REMOTE_RECENT),
        Route::Window<&CSelectCloudDlg::OnSize>(WM_SIZE),
        Route::Window<&CSelectCloudDlg::OnDestroy>(WM_DESTROY),
    };
    return entries;
}
