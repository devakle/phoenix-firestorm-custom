/**
 * @file alfloaterparcelim.cpp
 * @brief Floater to send IMs to all non-friend residents in the current parcel.
 *
 * $LicenseInfo:2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Alchemy Contributors
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alfloaterparcelim.h"

#include "llcheckboxctrl.h"
#include "lltexteditor.h"
#include "llflatlistview.h"
#include "llbutton.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "llavatarnamecache.h"
#include "llworld.h"
#include "llviewerparcelmgr.h"
#include "llcallingcard.h"
#include "llmutelist.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "lluictrlfactory.h"
#include "llsdutil.h"
#include "lldate.h"
#include "llimview.h"
#include "llviewerregion.h"
#include <set>

static const std::string SETTING_MESSAGE("ALParcelIMMessage");
static const std::string SETTING_INTERVAL("ALParcelIMSendInterval");
static const std::string SETTING_COOLDOWN("ALParcelIMCooldown");
static const std::string SETTING_EXCLUSIONS("ALParcelIMExclusions");

std::map<LLUUID, F64> ALFloaterParcelIM::sLastSentTimes;
std::map<LLUUID, F64> ALFloaterParcelIM::sFirstSeenTimes;

static constexpr F64 MIN_PARCEL_DWELL_SECONDS = 600.0; // 10 minutes

static void setTooltipIfTruncated(LLTextBox* text_box, const std::string& full_text)
{
    if (!text_box) return;

    const F32 text_width = text_box->getFont() ? text_box->getFont()->getWidthF32(full_text.c_str())
                                               : 0.f;
    const F32 available_width = (F32)text_box->getRect().getWidth();
    text_box->setToolTip(text_width > available_width ? full_text : LLStringUtil::null);
}

static bool isAvatarStillInCurrentParcel(const LLUUID& avatar_id)
{
    const LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region) return false;
    const LLUUID scope_region_id = agent_region->getRegionID();

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);
    for (size_t i = 0; i < avatar_ids.size(); ++i)
    {
        if (avatar_ids[i] == avatar_id)
        {
            const LLVector3d& global_pos = avatar_positions[i];
            const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
            if (!region || region->getRegionID() != scope_region_id) return false;
            if (!LLViewerParcelMgr::getInstance()->inAgentParcel(global_pos)) return false;
            return true;
        }
    }
    return false;
}

static bool hasDwelledAtLeast10Min(const LLUUID& avatar_id, F64 now)
{
    auto it = ALFloaterParcelIM::sFirstSeenTimes.find(avatar_id);
    if (it == ALFloaterParcelIM::sFirstSeenTimes.end()) return false;
    return (now - it->second) >= MIN_PARCEL_DWELL_SECONDS;
}

// Global dwell tracker: updates sFirstSeenTimes from nearby/LLWorld even when floater is closed.
// Mimics ALFloaterFriendsHere grace logic: 60s for parcel leave, 8s for region leave.
static std::map<LLUUID, F64> sParcelAbsentSince;
static std::map<LLUUID, F64> sRegionAbsentSince;
static constexpr F64 kParcelGraceSeconds = 60.0;
static constexpr F64 kRegionResetSeconds = 8.0;

class ParcelDwellTracker : public LLEventTimer
{
public:
    ParcelDwellTracker() : LLEventTimer(5.0f) {}
    bool tick() override
    {
        const LLViewerRegion* agent_region = gAgent.getRegion();
        if (!agent_region) return false;
        const LLUUID scope_region_id = agent_region->getRegionID();
        const F64 now = LLDate::now().secondsSinceEpoch();

        uuid_vec_t avatar_ids;
        std::vector<LLVector3d> avatar_positions;
        LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);

        std::set<LLUUID> avatars_in_region;
        std::set<LLUUID> avatars_in_parcel;
        for (size_t i = 0; i < avatar_ids.size(); ++i)
        {
            const LLUUID& id = avatar_ids[i];
            if (id.isNull() || id == gAgent.getID()) continue;
            const LLVector3d& global_pos = avatar_positions[i];
            const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
            if (!region || region->getRegionID() != scope_region_id) continue;
            avatars_in_region.insert(id);
            if (LLViewerParcelMgr::getInstance()->inAgentParcel(global_pos))
            {
                avatars_in_parcel.insert(id);
            }
        }

        // New or still present in parcel: record first seen, clear absences
        for (const LLUUID& id : avatars_in_parcel)
        {
            if (ALFloaterParcelIM::sFirstSeenTimes.find(id) == ALFloaterParcelIM::sFirstSeenTimes.end())
            {
                ALFloaterParcelIM::sFirstSeenTimes[id] = now;
            }
            sParcelAbsentSince.erase(id);
            sRegionAbsentSince.erase(id);
        }

        // Copy keys to avoid mutation while iterating
        std::vector<LLUUID> tracked_ids;
        tracked_ids.reserve(ALFloaterParcelIM::sFirstSeenTimes.size());
        for (auto& kv : ALFloaterParcelIM::sFirstSeenTimes) tracked_ids.push_back(kv.first);

        for (const LLUUID& id : tracked_ids)
        {
            if (avatars_in_parcel.find(id) != avatars_in_parcel.end()) continue;

            if (avatars_in_region.find(id) != avatars_in_region.end())
            {
                // Left parcel but still in region -> grace 60s
                auto it = sParcelAbsentSince.find(id);
                if (it == sParcelAbsentSince.end())
                {
                    sParcelAbsentSince[id] = now;
                }
                else if (now - it->second >= kParcelGraceSeconds)
                {
                    ALFloaterParcelIM::sFirstSeenTimes.erase(id);
                    sParcelAbsentSince.erase(id);
                }
                sRegionAbsentSince.erase(id);
            }
            else
            {
                // Left region entirely -> grace 8s
                auto it = sRegionAbsentSince.find(id);
                if (it == sRegionAbsentSince.end())
                {
                    sRegionAbsentSince[id] = now;
                }
                else if (now - it->second >= kRegionResetSeconds)
                {
                    ALFloaterParcelIM::sFirstSeenTimes.erase(id);
                    sParcelAbsentSince.erase(id);
                    sRegionAbsentSince.erase(id);
                }
                // Also clear parcel absent since we are in region-absent state
                // (kept for completeness)
            }
        }
        return false; // keep ticking
    }
};

static ParcelDwellTracker sParcelDwellTrackerInstance;

ALParcelIMContactItem::ALParcelIMContactItem(const LLUUID& avatar_id)
    : LLPanel()
    , mAvatarID(avatar_id)
{
    buildFromFile("panel_al_parcel_im_contact.xml");
}

bool ALParcelIMContactItem::postBuild()
{
    mEnabledCheck = getChild<LLCheckBoxCtrl>("contact_enabled");
    mNameText = getChild<LLTextBox>("contact_name");
    mStatusText = getChild<LLTextBox>("contact_status");

    // Load exclusion state (default is checked/enabled)
    const LLSD exclusions = gSavedPerAccountSettings.getLLSD(SETTING_EXCLUSIONS);
    bool is_excluded = exclusions[mAvatarID.asString()].asBoolean();
    mEnabledCheck->setValue(!is_excluded);

    mEnabledCheck->setCommitCallback(boost::bind(&ALParcelIMContactItem::onCheckCommit, this));

    return true;
}

void ALParcelIMContactItem::setAvatarName(const std::string& display_name, const std::string& account_name)
{
    mAvatarName = display_name;
    mAccountName = account_name;

    std::string label = display_name;
    if (!display_name.empty() && !account_name.empty())
    {
        label += " (" + account_name + ")";
    }
    mNameText->setText(label);
    setTooltipIfTruncated(mNameText, label);
}

void ALParcelIMContactItem::updateStatus(F64 now, F64 cooldown_duration)
{
    if (!mStatusText) return;

    auto it = ALFloaterParcelIM::sLastSentTimes.find(mAvatarID);
    if (it != ALFloaterParcelIM::sLastSentTimes.end())
    {
        F64 elapsed = now - it->second;
        if (elapsed < cooldown_duration)
        {
            F64 remaining_mins = (cooldown_duration - elapsed) / 60.0;
            mStatusText->setText(llformat("Cooldown (~%.0f m)", remaining_mins < 1.0 ? 1.0 : remaining_mins));
            return;
        }
    }

    mStatusText->setText(std::string("Ready"));
}

bool ALParcelIMContactItem::isSelected() const
{
    return mEnabledCheck ? mEnabledCheck->get() : false;
}

void ALParcelIMContactItem::setSelected(bool select)
{
    if (mEnabledCheck)
    {
        mEnabledCheck->setValue(select);
        onCheckCommit();
    }
}

void ALParcelIMContactItem::onCheckCommit()
{
    LLSD exclusions = gSavedPerAccountSettings.getLLSD(SETTING_EXCLUSIONS);
    bool is_excluded = !isSelected();
    if (is_excluded)
    {
        exclusions[mAvatarID.asString()] = true;
    }
    else
    {
        exclusions.erase(mAvatarID.asString());
    }
    gSavedPerAccountSettings.setLLSD(SETTING_EXCLUSIONS, exclusions);
}


ALFloaterParcelIM::ALFloaterParcelIM(const LLSD& key)
    : LLFloater(key)
{}

ALFloaterParcelIM::~ALFloaterParcelIM()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
}

bool ALFloaterParcelIM::postBuild()
{
    mMessageEdit = getChild<LLTextEditor>("im_message_text");
    mIntervalSpinner = getChild<LLSpinCtrl>("send_interval_spinner");
    mCooldownSpinner = getChild<LLSpinCtrl>("cooldown_spinner");
    mResidentList = getChild<LLFlatListView>("resident_list");
    mSendBtn = getChild<LLButton>("send_btn");
    mRefreshBtn = getChild<LLButton>("refresh_btn");
    mToggleAllBtn = getChild<LLButton>("toggle_all_btn");
    mStatusText = getChild<LLTextBox>("status_text");

    // Load initial values
    mMessageEdit->setText(gSavedPerAccountSettings.getString(SETTING_MESSAGE));
    S32 interval = gSavedPerAccountSettings.getS32(SETTING_INTERVAL);
    if (interval < 1) interval = 1;
    mIntervalSpinner->setValue(interval);

    S32 cooldown = gSavedPerAccountSettings.getS32(SETTING_COOLDOWN);
    if (cooldown < 1) cooldown = 30;
    mCooldownSpinner->setValue(cooldown);

    // Connect callbacks
    mSendBtn->setClickedCallback(boost::bind(&ALFloaterParcelIM::onClickSend, this));
    mRefreshBtn->setClickedCallback(boost::bind(&ALFloaterParcelIM::onClickRefresh, this));
    mToggleAllBtn->setClickedCallback(boost::bind(&ALFloaterParcelIM::onClickToggleAll, this));

    mMessageEdit->setCommitCallback(boost::bind(&ALFloaterParcelIM::onSettingsCommit, this));
    mIntervalSpinner->setCommitCallback(boost::bind(&ALFloaterParcelIM::onSettingsCommit, this));
    mCooldownSpinner->setCommitCallback(boost::bind(&ALFloaterParcelIM::onSettingsCommit, this));

    return true;
}

void ALFloaterParcelIM::onOpen(const LLSD& key)
{
    populateList();
}

void ALFloaterParcelIM::onClose(bool app_quitting)
{
    stopSending();
    onSettingsCommit();
}

void ALFloaterParcelIM::onSettingsCommit()
{
    if (mMessageEdit && mIntervalSpinner && mCooldownSpinner)
    {
        gSavedPerAccountSettings.setString(SETTING_MESSAGE, mMessageEdit->getText());
        gSavedPerAccountSettings.setS32(SETTING_INTERVAL, (S32)mIntervalSpinner->getValue().asInteger());
        gSavedPerAccountSettings.setS32(SETTING_COOLDOWN, (S32)mCooldownSpinner->getValue().asInteger());
    }
}

void ALFloaterParcelIM::populateList()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
    mAvatarNameConnections.clear();
    mResidentList->clear();

    const LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region) return;
    const LLUUID scope_region_id = agent_region->getRegionID();

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);

    S32 count = 0;
    F64 now = LLDate::now().secondsSinceEpoch();
    F64 cooldown_duration = gSavedPerAccountSettings.getS32(SETTING_COOLDOWN) * 60.0;

    for (size_t i = 0; i < avatar_ids.size(); ++i)
    {
        const LLUUID& id = avatar_ids[i];
        if (id.isNull() || id == gAgent.getID()) continue;

        const LLVector3d& global_pos = avatar_positions[i];
        const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
        if (!region || region->getRegionID() != scope_region_id) continue;

        // Filters: Muted or Friends must not receive the IM
        if (LLMuteList::getInstance()->isMuted(id)) continue;
        if (LLAvatarTracker::instance().isBuddy(id)) continue;

        // Verify parcel
        if (!LLViewerParcelMgr::getInstance()->inAgentParcel(global_pos)) continue;

        // Track first time seen in parcel for 10-min dwell check
        if (sFirstSeenTimes.find(id) == sFirstSeenTimes.end())
        {
            sFirstSeenTimes[id] = now;
        }

        ALParcelIMContactItem* item = new ALParcelIMContactItem(id);
        mResidentList->addItem(item, LLSD(id));
        item->updateStatus(now, cooldown_duration);

        auto connection = LLAvatarNameCache::get(id,
            boost::bind(&ALFloaterParcelIM::onAvatarNameLoaded, this, _1, _2));
        mAvatarNameConnections.push_back(connection);
        ++count;
    }

    if (mStatusText && !mIsSending)
    {
        mStatusText->setText(llformat("Found %d residents in the current parcel.", count));
    }
}

void ALFloaterParcelIM::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    ALParcelIMContactItem* item =
        mResidentList->getTypedItemByValue<ALParcelIMContactItem>(LLSD(agent_id));
    if (item)
    {
        item->setAvatarName(avname.getDisplayName(), avname.getAccountName());
    }
}

void ALFloaterParcelIM::onClickRefresh()
{
    if (mIsSending) return;
    populateList();
}

void ALFloaterParcelIM::onClickToggleAll()
{
    if (mIsSending) return;

    std::vector<LLPanel*> items;
    mResidentList->getItems(items);
    if (items.empty()) return;

    // Check if at least one is unchecked
    bool all_selected = true;
    for (LLPanel* panel : items)
    {
        ALParcelIMContactItem* item = dynamic_cast<ALParcelIMContactItem*>(panel);
        if (item && !item->isSelected())
        {
            all_selected = false;
            break;
        }
    }

    // Toggle all
    for (LLPanel* panel : items)
    {
        ALParcelIMContactItem* item = dynamic_cast<ALParcelIMContactItem*>(panel);
        if (item)
        {
            item->setSelected(!all_selected);
        }
    }
}

void ALFloaterParcelIM::onClickSend()
{
    if (mIsSending)
    {
        stopSending();
        return;
    }

    startSending();
}

void ALFloaterParcelIM::startSending()
{
    mPendingRecipients.clear();
    mCurrentIndex = 0;

    std::vector<LLPanel*> items;
    mResidentList->getItems(items);
    
    F64 now = LLDate::now().secondsSinceEpoch();
    F64 cooldown_duration = gSavedPerAccountSettings.getS32(SETTING_COOLDOWN) * 60.0;

    for (LLPanel* panel : items)
    {
        ALParcelIMContactItem* item = dynamic_cast<ALParcelIMContactItem*>(panel);
        if (item && item->isSelected())
        {
            LLUUID id = item->getAvatarID();

            // Skip if on cooldown
            auto it = sLastSentTimes.find(id);
            if (it != sLastSentTimes.end() && (now - it->second) < cooldown_duration)
            {
                continue;
            }

            // Must still be in current parcel
            if (!isAvatarStillInCurrentParcel(id))
            {
                continue;
            }

            // Must have been in parcel at least 10 minutes
            if (!hasDwelledAtLeast10Min(id, now))
            {
                continue;
            }

            mPendingRecipients.push_back(id);
        }
    }

    if (mPendingRecipients.empty())
    {
        if (mStatusText)
        {
            mStatusText->setText(std::string("No eligible selected recipients to send message to."));
        }
        return;
    }

    mIsSending = true;
    mSendBtn->setLabel("Cancel");
    mRefreshBtn->setEnabled(false);
    mToggleAllBtn->setEnabled(false);
    mMessageEdit->setEnabled(false);
    mIntervalSpinner->setEnabled(false);
    mCooldownSpinner->setEnabled(false);

    // Save configuration settings
    onSettingsCommit();

    F32 interval = (F32)mIntervalSpinner->getValue().asReal();
    mPacer = std::make_unique<IMPacer>(this, interval);

    // Send the first IM immediately
    sendNextIM();
}

void ALFloaterParcelIM::stopSending()
{
    mIsSending = false;
    mPacer.reset();
    mPendingRecipients.clear();

    mSendBtn->setLabel("Send");
    mRefreshBtn->setEnabled(true);
    mToggleAllBtn->setEnabled(true);
    mMessageEdit->setEnabled(true);
    mIntervalSpinner->setEnabled(true);
    mCooldownSpinner->setEnabled(true);

    populateList();
}

bool ALFloaterParcelIM::sendNextIM()
{
    // Skip any recipients that no longer satisfy parcel/dwell checks before sending
    F64 now_check = LLDate::now().secondsSinceEpoch();
    while (mCurrentIndex < mPendingRecipients.size())
    {
        const LLUUID& cand = mPendingRecipients[mCurrentIndex];
        if (!isAvatarStillInCurrentParcel(cand) || !hasDwelledAtLeast10Min(cand, now_check))
        {
            ++mCurrentIndex;
            continue;
        }
        break;
    }

    if (mCurrentIndex >= mPendingRecipients.size())
    {
        stopSending();
        if (mStatusText)
        {
            mStatusText->setText(std::string("Send process finished successfully."));
        }
        return false;
    }

    LLUUID recipient_id = mPendingRecipients[mCurrentIndex];
    std::string base_msg = mMessageEdit->getText();

    // Perform substitution of [name]/[alias]
    LLAvatarName avname;
    std::string name_val = recipient_id.asString();
    if (LLAvatarNameCache::get(recipient_id, &avname))
    {
        name_val = avname.getDisplayName();
    }
    
    std::string message = base_msg;
    auto substitute = [&message](const std::string& token, const std::string& value)
    {
        size_t pos = 0;
        while ((pos = message.find(token, pos)) != std::string::npos)
        {
            message.replace(pos, token.length(), value);
            pos += value.length();
        }
    };
    substitute("[name]", name_val);
    substitute("[alias]", name_val);

    // Send IM
    const LLUUID session_id = LLIMMgr::computeSessionID(IM_NOTHING_SPECIAL, recipient_id);
    LLIMModel::sendMessage(message, session_id, recipient_id, IM_NOTHING_SPECIAL);

    // Record last sent time
    F64 now = LLDate::now().secondsSinceEpoch();
    sLastSentTimes[recipient_id] = now;

    if (mStatusText)
    {
        mStatusText->setText(llformat("Sending: %d/%d completed.", (S32)(mCurrentIndex + 1), (S32)mPendingRecipients.size()));
    }

    // Update row status if visible in the list
    ALParcelIMContactItem* item = mResidentList->getTypedItemByValue<ALParcelIMContactItem>(LLSD(recipient_id));
    if (item)
    {
        item->updateStatus(now, gSavedPerAccountSettings.getS32(SETTING_COOLDOWN) * 60.0);
    }

    mCurrentIndex++;
    return true;
}

bool ALFloaterParcelIM::IMPacer::tick()
{
    if (!mParent->mIsSending)
    {
        return true; // Stop ticking
    }

    mParent->sendNextIM();
    return false; // Keep ticking
}
