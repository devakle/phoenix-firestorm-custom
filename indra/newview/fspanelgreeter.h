/**
 * @file fspanelgreeter.h
 * @brief Greeter panel - shows nearby strangers not yet greeted
 */

#ifndef FS_PANELGREETER_H
#define FS_PANELGREETER_H

#include "llpanel.h"
#include "fsradar.h"
#include <set>

class LLAvatarList;
class LLFilterEditor;
class LLLineEditor;
class LLButton;
class LLTextBox;

class FSPanelGreeter : public LLPanel
{
    LOG_CLASS(FSPanelGreeter);
public:
    FSPanelGreeter();
    ~FSPanelGreeter() override;

    bool postBuild() override;
    bool handleKeyHere(KEY key, MASK mask) override { return LLPanel::handleKeyHere(key, mask); }

private:
    void updateList(const std::vector<LLSD>& entries, const LLSD& stats);
    void rebuildList();
    void updateButtons();

    void onGreetClicked();
    void onResetClicked();
    void onFilterEdit(const std::string& search_string);
    void onMessageChanged();

    LLAvatarList*   mGreeterList{nullptr};
    LLFilterEditor* mFilterEditor{nullptr};
    LLLineEditor*   mMessageEditor{nullptr};
    LLButton*       mGreetButton{nullptr};
    LLButton*       mResetButton{nullptr};
    LLTextBox*      mCountText{nullptr};

    std::set<LLUUID> mGreeted;
    std::vector<LLUUID> mCurrentUngreeted;
    std::map<LLUUID, std::string> mIdToName;

    boost::signals2::connection mRadarConn;
    boost::signals2::connection mGreeterMessageConn;
};

#endif // FS_PANELGREETER_H
