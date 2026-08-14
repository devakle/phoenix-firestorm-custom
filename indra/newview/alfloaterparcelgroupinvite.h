/**
 * @file alfloaterparcelgroupinvite.h
 * @brief Floater to send group invitations to every resident on the current parcel
 *
 * $LicenseInfo:2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Alchemy Contributors
 * $/LicenseInfo$
 */

#ifndef AL_FLOATER_PARCEL_GROUP_INVITE_H
#define AL_FLOATER_PARCEL_GROUP_INVITE_H

#include "llfloater.h"
#include "lleventtimer.h"
#include "llgroupmgr.h"

class LLButton;
class LLCheckBoxCtrl;
class LLComboBox;
class LLSpinCtrl;
class LLTextBox;

class ALFloaterParcelGroupInvite final : public LLFloater, public LLEventTimer, public LLParticularGroupObserver
{
public:
    ALFloaterParcelGroupInvite(const LLSD& key);
    ~ALFloaterParcelGroupInvite() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    bool tick() override;

    void changed(const LLUUID& group_id, LLGroupChange gc) override;

private:
    void refreshGroupCombo();
    void onGroupCommitted();
    void onCooldownCommitted();
    void onActiveChanged();
    void onClickSendNow();

    void requestGroupMembers();
    bool isGroupDataLoaded() const;
    void sendRound();
    void updateStatus();
    void registerGroupObserver();

    LLComboBox*    mGroupCombo = nullptr;
    LLCheckBoxCtrl* mActiveCheck = nullptr;
    LLSpinCtrl*    mCooldownSpin = nullptr;
    LLButton*      mSendNowBtn = nullptr;
    LLTextBox*     mStatusText = nullptr;

    LLUUID mGroupID;
    F64    mLastRoundTime = 0.0;
    F64    mMembersReadyTime = 0.0;
    bool   mWaitingForMembers = false;
};

#endif // AL_FLOATER_PARCEL_GROUP_INVITE_H