/**
 * @file alfloaterparcelim.h
 * @brief Floater to send IMs to all non-friend residents in the current parcel.
 *
 * $LicenseInfo:2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Alchemy Contributors
 * $/LicenseInfo$
 */

#ifndef AL_FLOATER_PARCEL_IM_H
#define AL_FLOATER_PARCEL_IM_H

#include "llfloater.h"
#include "lleventtimer.h"
#include <boost/signals2.hpp>
#include <vector>
#include <map>

class LLCheckBoxCtrl;
class LLTextEditor;
class LLFlatListView;
class LLButton;
class LLSpinCtrl;
class LLTextBox;
class LLAvatarName;

class ALParcelIMContactItem final : public LLPanel
{
public:
    ALParcelIMContactItem(const LLUUID& avatar_id);
    bool postBuild() override;

    const LLUUID& getAvatarID() const { return mAvatarID; }
    void setAvatarName(const std::string& display_name, const std::string& account_name);
    void updateStatus(F64 now, F64 cooldown_duration);

    bool isSelected() const;
    void setSelected(bool select);

private:
    void onCheckCommit();

    LLUUID          mAvatarID;
    LLCheckBoxCtrl* mEnabledCheck = nullptr;
    LLTextBox*      mNameText = nullptr;
    LLTextBox*      mStatusText = nullptr;
    std::string     mAvatarName;
    std::string     mAccountName;
};

class ALFloaterParcelIM final : public LLFloater
{
public:
    ALFloaterParcelIM(const LLSD& key);
    ~ALFloaterParcelIM() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

    // Static mapping of resident ID to last send timestamp in seconds
    static std::map<LLUUID, F64> sLastSentTimes;
    // First time an avatar was seen in the current parcel (for 10-min dwell check)
    static std::map<LLUUID, F64> sFirstSeenTimes;

private:
    void populateList();
    void onClickRefresh();
    void onClickSend();
    void onClickToggleAll();
    void onSettingsCommit();
    void onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname);

    // IM Sending Pacer class
    class IMPacer final : public LLEventTimer
    {
    public:
        IMPacer(ALFloaterParcelIM* parent, F32 interval_seconds)
            : LLEventTimer(interval_seconds)
            , mParent(parent)
        {}

        bool tick() override;

    private:
        ALFloaterParcelIM* mParent;
    };

    void startSending();
    void stopSending();
    bool sendNextIM();

    LLTextEditor*   mMessageEdit = nullptr;
    LLSpinCtrl*     mIntervalSpinner = nullptr;
    LLSpinCtrl*     mCooldownSpinner = nullptr;
    LLFlatListView* mResidentList = nullptr;
    LLButton*       mSendBtn = nullptr;
    LLButton*       mRefreshBtn = nullptr;
    LLButton*       mToggleAllBtn = nullptr;
    LLTextBox*      mStatusText = nullptr;

    std::unique_ptr<IMPacer> mPacer;
    std::vector<LLUUID>      mPendingRecipients;
    size_t                   mCurrentIndex = 0;
    bool                     mIsSending = false;

    std::vector<boost::signals2::connection> mAvatarNameConnections;
};

#endif // AL_FLOATER_PARCEL_IM_H
