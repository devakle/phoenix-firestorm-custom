/**
 * @file alfloaterparcelgroupinvite.cpp
 * @brief Floater to send group invitations to every resident on the current parcel
 *
 * $LicenseInfo:2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Alchemy Contributors
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alfloaterparcelgroupinvite.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lldate.h"
#include "llgroupactions.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "roles_constants.h"

#include <algorithm>

static const std::string AL_PARCEL_GROUP_INVITE_GROUP_SETTING("ALParcelGroupInviteGroupID");
static const std::string AL_PARCEL_GROUP_INVITE_COOLDOWN_SETTING("ALParcelGroupInviteCooldownMinutes");
static const std::string AL_PARCEL_GROUP_INVITE_ACTIVE_SETTING("ALParcelGroupInviteActive");
static const F32          DEFAULT_COOLDOWN_MINUTES(10.f);
static const S32          MAX_INVITES_PER_ROUND = 100;   // Server cap per request.
static const F32          MEMBERS_PAGE_GRACE_SECONDS(3.f); // Allow the member list to finish paging before the first round.

ALFloaterParcelGroupInvite::ALFloaterParcelGroupInvite(const LLSD& key)
    : LLFloater(key)
    , LLEventTimer(1.f)
{
}

ALFloaterParcelGroupInvite::~ALFloaterParcelGroupInvite()
{
    if (mGroupID.notNull())
    {
        LLGroupMgr::getInstance()->removeObserver(mGroupID, this);
    }
}

bool ALFloaterParcelGroupInvite::postBuild()
{
    mGroupCombo = getChild<LLComboBox>("group_combo");
    mActiveCheck = getChild<LLCheckBoxCtrl>("active_check");
    mCooldownSpin = getChild<LLSpinCtrl>("cooldown_spin");
    mSendNowBtn = getChild<LLButton>("send_now_btn");
    mStatusText = getChild<LLTextBox>("status_text");

    mGroupCombo->setCommitCallback(boost::bind(&ALFloaterParcelGroupInvite::onGroupCommitted, this));
    mCooldownSpin->setCommitCallback(boost::bind(&ALFloaterParcelGroupInvite::onCooldownCommitted, this));
    mActiveCheck->setCommitCallback(boost::bind(&ALFloaterParcelGroupInvite::onActiveChanged, this));
    mSendNowBtn->setClickedCallback(boost::bind(&ALFloaterParcelGroupInvite::onClickSendNow, this));

    mGroupID = LLUUID(gSavedPerAccountSettings.getString(AL_PARCEL_GROUP_INVITE_GROUP_SETTING));
    if (const F32 saved_cooldown = gSavedPerAccountSettings.getF32(AL_PARCEL_GROUP_INVITE_COOLDOWN_SETTING);
        saved_cooldown > 0.f)
    {
        mCooldownSpin->set(saved_cooldown);
    }
    mActiveCheck->set(gSavedPerAccountSettings.getBOOL(AL_PARCEL_GROUP_INVITE_ACTIVE_SETTING));

    refreshGroupCombo();
    registerGroupObserver();

    if (mActiveCheck->get() && mGroupID.notNull())
    {
        requestGroupMembers();
    }

    updateStatus();
    return TRUE;
}

void ALFloaterParcelGroupInvite::onOpen(const LLSD& key)
{
    // Refresh in case group membership changed while the floater was closed.
    refreshGroupCombo();
    mEventTimer.start();
    updateStatus();
}

void ALFloaterParcelGroupInvite::onClose(bool app_quitting)
{
    mEventTimer.stop();
    mWaitingForMembers = false;
}

bool ALFloaterParcelGroupInvite::tick()
{
    if (mActiveCheck && !mActiveCheck->get())
    {
        return FALSE;
    }

    updateStatus();

    if (mGroupID.isNull())
    {
        return FALSE;
    }
    if (mWaitingForMembers)
    {
        return FALSE;
    }

    const F64 now = LLDate::now().secondsSinceEpoch();

    // Give a just-loaded member list time to finish paging before the first round.
    if (mLastRoundTime <= 0.0 && now - mMembersReadyTime < MEMBERS_PAGE_GRACE_SECONDS)
    {
        return FALSE;
    }

    const F64 next_round = mLastRoundTime + (F64)mCooldownSpin->getValueF32() * 60.0;
    if (now >= next_round)
    {
        sendRound();
        mLastRoundTime = LLDate::now().secondsSinceEpoch();
    }

    return FALSE;
}

void ALFloaterParcelGroupInvite::changed(const LLUUID& group_id, LLGroupChange gc)
{
    if (gc != GC_MEMBER_DATA || group_id != mGroupID)
    {
        return;
    }
    if (mWaitingForMembers && isGroupDataLoaded())
    {
        mWaitingForMembers = false;
        mMembersReadyTime = LLDate::now().secondsSinceEpoch();
    }
}

void ALFloaterParcelGroupInvite::refreshGroupCombo()
{
    mGroupCombo->removeall();
    for (const LLGroupData& group_data : gAgent.mGroups)
    {
        if (!gAgent.hasPowerInGroup(group_data.mID, GP_MEMBER_INVITE))
        {
            continue;
        }
        mGroupCombo->add(group_data.mName, LLSD(group_data.mID), ADD_BOTTOM, true);
    }
    mGroupCombo->sortByName(true);

    const bool has_invite_groups = mGroupCombo->getItemCount() > 0;
    mActiveCheck->setEnabled(has_invite_groups);
    mSendNowBtn->setEnabled(has_invite_groups);

    if (has_invite_groups)
    {
        if (mGroupID.notNull() && mGroupCombo->getItemByValue(LLSD(mGroupID)))
        {
            mGroupCombo->setValue(LLSD(mGroupID));
        }
        else
        {
            mGroupID = mGroupCombo->getValue().asUUID();
        }
    }
    else
    {
        mGroupID = LLUUID::null;
    }
}

void ALFloaterParcelGroupInvite::onGroupCommitted()
{
    const LLUUID old_group_id = mGroupID;
    mGroupID = mGroupCombo->getValue().asUUID();
    gSavedPerAccountSettings.setString(AL_PARCEL_GROUP_INVITE_GROUP_SETTING, mGroupID.asString());

    if (mGroupID != old_group_id)
    {
        if (old_group_id.notNull())
        {
            LLGroupMgr::getInstance()->removeObserver(old_group_id, this);
        }
        mLastRoundTime = 0.0;
        mMembersReadyTime = 0.0;
        mWaitingForMembers = false;
        registerGroupObserver();
        if (mActiveCheck->get() && mGroupID.notNull())
        {
            requestGroupMembers();
        }
    }

    updateStatus();
}

void ALFloaterParcelGroupInvite::onCooldownCommitted()
{
    gSavedPerAccountSettings.setF32(AL_PARCEL_GROUP_INVITE_COOLDOWN_SETTING, mCooldownSpin->getValueF32());
}

void ALFloaterParcelGroupInvite::onActiveChanged()
{
    const bool active = mActiveCheck && mActiveCheck->get();
    gSavedPerAccountSettings.setBOOL(AL_PARCEL_GROUP_INVITE_ACTIVE_SETTING, active);

    if (active)
    {
        mLastRoundTime = 0.0;
        mMembersReadyTime = 0.0;
        mWaitingForMembers = false;
        if (mGroupID.notNull())
        {
            requestGroupMembers();
        }
    }
    else
    {
        mWaitingForMembers = false;
    }

    updateStatus();
}

void ALFloaterParcelGroupInvite::onClickSendNow()
{
    if (mGroupID.isNull())
    {
        updateStatus();
        return;
    }
    if (!isGroupDataLoaded())
    {
        requestGroupMembers();
        updateStatus();
        return;
    }

    sendRound();
    mLastRoundTime = LLDate::now().secondsSinceEpoch();
}

void ALFloaterParcelGroupInvite::requestGroupMembers()
{
    if (isGroupDataLoaded())
    {
        mWaitingForMembers = false;
        mMembersReadyTime = LLDate::now().secondsSinceEpoch() - MEMBERS_PAGE_GRACE_SECONDS;
    }
    else
    {
        mWaitingForMembers = true;
        LLGroupMgr::getInstance()->sendGroupMembersRequest(mGroupID);
    }
}

bool ALFloaterParcelGroupInvite::isGroupDataLoaded() const
{
    LLGroupMgrGroupData* group_data = LLGroupMgr::getInstance()->getGroupData(mGroupID);
    return group_data && !group_data->mMembers.empty();
}

void ALFloaterParcelGroupInvite::sendRound()
{
    if (mGroupID.isNull())
    {
        return;
    }

    const LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region)
    {
        return;
    }
    const LLUUID region_id = agent_region->getRegionID();

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);

    std::map<LLUUID, LLUUID> role_member_pairs;
    for (S32 i = 0; i < (S32)avatar_ids.size(); ++i)
    {
        const LLUUID& avatar_id = avatar_ids[i];
        if (avatar_id == gAgent.getID())
        {
            continue;
        }

        const LLVector3d& global_pos = avatar_positions[i];
        const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
        if (!region || region->getRegionID() != region_id)
        {
            continue;
        }
        if (!LLViewerParcelMgr::getInstance()->inAgentParcel(global_pos))
        {
            continue;
        }
        if (LLGroupActions::isAvatarMemberOfGroup(mGroupID, avatar_id))
        {
            continue;
        }
        if (role_member_pairs.size() >= (S32)MAX_INVITES_PER_ROUND)
        {
            break;
        }

        // LLUUID::null role = Everyone role.
        role_member_pairs[avatar_id] = LLUUID::null;
    }

    if (role_member_pairs.empty())
    {
        mStatusText->setText(getString("StatusNoneToInvite"));
        return;
    }

    LLGroupMgr::getInstance()->sendGroupMemberInvites(mGroupID, role_member_pairs);

    LLStringUtil::format_map_t args;
    args["COUNT"] = llformat("%u", (U32)role_member_pairs.size());
    args["MINUTES"] = llformat("%.0f", mCooldownSpin->getValueF32());
    mStatusText->setText(getString("StatusRoundDone", args));
}

void ALFloaterParcelGroupInvite::updateStatus()
{
    if (!mStatusText)
    {
        return;
    }

    if (mGroupCombo->getItemCount() == 0)
    {
        mStatusText->setText(getString("StatusNoInvitePower"));
        return;
    }
    if (mGroupID.isNull())
    {
        mStatusText->setText(getString("StatusNoGroup"));
        return;
    }
    if (mWaitingForMembers)
    {
        mStatusText->setText(getString("StatusLoadingMembers"));
        return;
    }
    if (!(mActiveCheck && mActiveCheck->get()))
    {
        mStatusText->setText(getString("StatusIdle"));
        return;
    }
    if (mLastRoundTime <= 0.0)
    {
        mStatusText->setText(getString("StatusWaitingFirstRound"));
        return;
    }

    const F64 now = LLDate::now().secondsSinceEpoch();
    const F64 next_round = mLastRoundTime + (F64)mCooldownSpin->getValueF32() * 60.0;
    const S32 minutes_left = std::max((S32)((next_round - now) / 60.0) + 1, 0);

    LLStringUtil::format_map_t args;
    args["MINUTES"] = llformat("%d", minutes_left);
    mStatusText->setText(getString("StatusCountdown", args));
}

void ALFloaterParcelGroupInvite::registerGroupObserver()
{
    if (mGroupID.notNull())
    {
        LLGroupMgr::getInstance()->addObserver(mGroupID, this);
    }
}