/**
 * @file alfloaterclubinvite.h
 * @brief Floater to send a club invitation message to all selected friends
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#ifndef AL_FLOATER_CLUB_INVITE_H
#define AL_FLOATER_CLUB_INVITE_H

#include "llfloater.h"
#include <boost/signals2.hpp>

class LLCheckBoxCtrl;
class LLLineEditor;
class LLTextBox;
class LLTextEditor;
class LLSpinCtrl;
class LLFlatListView;
class LLAssetFilteredInventoryPanel;
class LLButton;
class LLAvatarName;
class LLFolderViewItem;

class ALFloaterClubInvite final : public LLFloater
{
public:
    ALFloaterClubInvite(const LLSD& key);
    ~ALFloaterClubInvite() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

private:
    void onObjectSelected(const std::deque<LLFolderViewItem*>& items, bool user_action);
    void onObjectSearch(LLLineEditor* caller, void* user_data);
    void onClickSend();
    void onClickStop();
    void onClickAliases();
    void onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname);

    void populateContacts();
    void onClickRefresh();
    void sendCoro(const std::vector<std::pair<LLUUID, std::string>>& recipients, F32 cooldown);

    static std::string composeMessage(const std::string& base, const std::string& club,
                                      const std::string& slurl, const std::string& name);

    bool isOnCooldown(const LLUUID& avatar_id) const;
    void markCooldown(const LLUUID& avatar_id);
    bool isInAgentRegion(const LLUUID& avatar_id) const;

    void onSendDelayCommit();
    void onResendCooldownCommit();

    bool                     mSending = false;
    bool                     mStopRequested = false;
    S32                      mSendGeneration = 1;
    LLUUID                   mObjectItemID;

    LLTextEditor*            mMessageEditor = nullptr;
    LLLineEditor*            mClubAlias = nullptr;
    LLLineEditor*            mClubSlurl = nullptr;
    LLSpinCtrl*              mCooldownSpin = nullptr;
    LLSpinCtrl*              mResendSpin = nullptr;
    LLFlatListView*          mContactList = nullptr;
    LLAssetFilteredInventoryPanel* mObjectPanel = nullptr;
    LLLineEditor*            mObjectSearch = nullptr;
    LLButton*                mSendBtn = nullptr;
    LLButton*                mRefreshBtn = nullptr;
    LLTextBox*               mStatusText = nullptr;

    std::vector<boost::signals2::connection> mAvatarNameConnections;
};

#endif // AL_FLOATER_CLUB_INVITE_H