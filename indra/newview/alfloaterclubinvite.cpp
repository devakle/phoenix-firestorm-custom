/**
 * @file alfloaterclubinvite.cpp
 * @brief Floater to send a club invitation message to all selected friends
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alfloaterclubinvite.h"

#include "llbutton.h"
#include "llcallingcard.h"
#include "llcheckboxctrl.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llflatlistview.h"
#include "llfloaterreg.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llgiveinventory.h"
#include "llimview.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "lllineeditor.h"
#include "llspinctrl.h"
#include "llstring.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "lldate.h"
#include "llviewermessage.h"
#include "llviewerinventory.h"
#include "llavatarnamecache.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "llworld.h"
#include "llviewerregion.h"

static const std::string TOKEN_CLUB("[club]");
static const std::string TOKEN_SLURL("[slurl]");
static const std::string TOKEN_NAME("[name]");
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");
static const std::string AL_CLUB_INVITE_COOLDOWN_SETTING("ALClubInviteCooldowns");
static const std::string AL_CLUB_INVITE_NEVER_IM_SETTING("ALClubInviteNeverIM");
static const std::string AL_CLUB_INVITE_SEND_DELAY_SETTING("ALClubInviteSendDelay");
static const std::string AL_CLUB_INVITE_RESEND_COOLDOWN_SETTING("ALClubInviteResendCooldownMinutes");
static const F32          DEFAULT_CLUB_INVITE_RESEND_COOLDOWN_MINUTES(30.f);

static F32 getSavedF32Setting(const std::string& name, F32 fallback)
{
    LLControlVariablePtr control = gSavedPerAccountSettings.getControl(name);
    return control ? (F32)control->getValue().asReal() : fallback;
}

class ALClubInviteContactItem final : public LLPanel
{
public:
    ALClubInviteContactItem(const LLUUID& avatar_id);

    bool postBuild() override;

    const LLUUID& getAvatarID() const { return mAvatarID; }
    bool isEnabled() const;
    void setAvatarName(const std::string& display_name, const std::string& account_name);
    std::string getAlias() const;
    void setOnCooldown(bool cooldown);

private:
    void onClickEditAlias();
    void onNeverIMChanged();
    void updateEnabledControls();
    void updateAliasDisplay();

    LLUUID      mAvatarID;
    LLCheckBoxCtrl* mEnabledCheck = nullptr;
    LLTextBox*  mNameText = nullptr;
    LLTextBox*  mAliasText = nullptr;
    LLButton*   mEditBtn = nullptr;
    LLCheckBoxCtrl* mNeverIMCheck = nullptr;
    std::string mAvatarName;
    bool        mOnCooldown = false;
    bool        mNeverIM = false;
};

ALClubInviteContactItem::ALClubInviteContactItem(const LLUUID& avatar_id)
    : LLPanel()
    , mAvatarID(avatar_id)
{
    buildFromFile("panel_al_club_invite_contact.xml");
}

bool ALClubInviteContactItem::postBuild()
{
    mEnabledCheck = getChild<LLCheckBoxCtrl>("contact_enabled");
    mNameText = getChild<LLTextBox>("contact_name");
    mAliasText = getChild<LLTextBox>("contact_alias");
    mEditBtn = getChild<LLButton>("contact_edit_btn");
    mNeverIMCheck = getChild<LLCheckBoxCtrl>("contact_no_im");
    setValue(LLSD(mAvatarID));

    updateAliasDisplay();

    const LLSD never_im = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_NEVER_IM_SETTING);
    mNeverIM = never_im[mAvatarID.asString()].asBoolean();
    if (mNeverIMCheck)
    {
        mNeverIMCheck->set(mNeverIM);
        mNeverIMCheck->setCommitCallback(boost::bind(&ALClubInviteContactItem::onNeverIMChanged, this));
    }
    updateEnabledControls();

    if (mEditBtn)
    {
        mEditBtn->setCommitCallback(boost::bind(&ALClubInviteContactItem::onClickEditAlias, this));
    }
    return true;
}

void ALClubInviteContactItem::onClickEditAlias()
{
    LLFloaterReg::showInstance("aliases", LLSD().with("avatar_id", mAvatarID));
}

bool ALClubInviteContactItem::isEnabled() const
{
    return !mNeverIM && !mOnCooldown && mEnabledCheck && mEnabledCheck->get();
}

void ALClubInviteContactItem::setAvatarName(const std::string& display_name,
                                            const std::string& account_name)
{
    mAvatarName = display_name;
    std::string label = display_name;
    if (!display_name.empty() && !account_name.empty())
    {
        label += " (" + account_name + ")";
    }
    if (mNameText)
    {
        mNameText->setText(label);
    }
    updateAliasDisplay();
}

std::string ALClubInviteContactItem::getAlias() const
{
    const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
    const std::string alias = aliases[mAvatarID.asString()].asString();
    return alias.empty() ? mAvatarName : alias;
}

void ALClubInviteContactItem::updateAliasDisplay()
{
    if (!mAliasText)
    {
        return;
    }
    const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
    const std::string alias = aliases[mAvatarID.asString()].asString();
    mAliasText->setText(alias.empty() ? mAvatarName : alias);
}

void ALClubInviteContactItem::setOnCooldown(bool cooldown)
{
    mOnCooldown = cooldown;
    updateEnabledControls();
}

void ALClubInviteContactItem::onNeverIMChanged()
{
    mNeverIM = mNeverIMCheck && mNeverIMCheck->get();
    LLSD never_im = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_NEVER_IM_SETTING);
    never_im[mAvatarID.asString()] = mNeverIM;
    gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_NEVER_IM_SETTING, never_im);
    updateEnabledControls();
}

void ALClubInviteContactItem::updateEnabledControls()
{
    if (mEnabledCheck)
    {
        mEnabledCheck->setEnabled(!mOnCooldown && !mNeverIM);
    }
}

ALFloaterClubInvite::ALFloaterClubInvite(const LLSD& key)
    : LLFloater(key)
{}

ALFloaterClubInvite::~ALFloaterClubInvite()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
}

bool ALFloaterClubInvite::postBuild()
{
    mMessageEditor = getChild<LLTextEditor>("message_editor");
    mClubAlias = getChild<LLLineEditor>("club_alias");
    mClubSlurl = getChild<LLLineEditor>("club_slurl");
    mCooldownSpin = getChild<LLSpinCtrl>("cooldown_spin");
    mResendSpin = getChild<LLSpinCtrl>("resend_cooldown_spin");
    mContactList = getChild<LLFlatListView>("contact_list");
    mStatusText = getChild<LLTextBox>("status_text");
    mObjectPanel = getChild<LLAssetFilteredInventoryPanel>("object_panel");

    mSendBtn = getChild<LLButton>("send_btn");
    mSendBtn->setCommitCallback(boost::bind(&ALFloaterClubInvite::onClickSend, this));
    getChild<LLButton>("stop_btn")->setCommitCallback(boost::bind(&ALFloaterClubInvite::onClickStop, this));
    getChild<LLButton>("stop_btn")->setEnabled(false);

    mRefreshBtn = getChild<LLButton>("refresh_btn");
    mRefreshBtn->setClickedCallback(boost::bind(&ALFloaterClubInvite::onClickRefresh, this));
    getChild<LLButton>("aliases_btn")->setClickedCallback(boost::bind(&ALFloaterClubInvite::onClickAliases, this));

    mCooldownSpin->setCommitCallback(boost::bind(&ALFloaterClubInvite::onSendDelayCommit, this));
    mResendSpin->setCommitCallback(boost::bind(&ALFloaterClubInvite::onResendCooldownCommit, this));
    mCooldownSpin->set(getSavedF32Setting(AL_CLUB_INVITE_SEND_DELAY_SETTING, 10.f));
    mResendSpin->set(getSavedF32Setting(AL_CLUB_INVITE_RESEND_COOLDOWN_SETTING,
                                        DEFAULT_CLUB_INVITE_RESEND_COOLDOWN_MINUTES));

    if (mObjectPanel)
    {
        mObjectPanel->setSelectCallback(boost::bind(&ALFloaterClubInvite::onObjectSelected, this, _1, _2));
    }
    mObjectSearch = getChild<LLLineEditor>("object_search");
    mObjectSearch->setKeystrokeCallback(
        boost::bind(&ALFloaterClubInvite::onObjectSearch, this, _1, _2), nullptr);

    return true;
}

void ALFloaterClubInvite::onOpen(const LLSD&)
{
    // A new open starts a fresh send cycle: any coroutine still running from a
    // previous cycle is stale (generation mismatch) and must not touch the UI.
    ++mSendGeneration;
    mSending = false;
    mStopRequested = false;
    mSendBtn->setEnabled(true);
    getChild<LLButton>("stop_btn")->setEnabled(false);
    populateContacts();
}

void ALFloaterClubInvite::onClose(bool /*app_quitting*/)
{
    mStopRequested = true;
}

void ALFloaterClubInvite::populateContacts()
{
    for (auto& connection : mAvatarNameConnections)
    {
        connection.disconnect();
    }
    mAvatarNameConnections.clear();
    mContactList->clear();

    LLAvatarTracker::buddy_map_t buddies;
    LLAvatarTracker::instance().copyBuddyList(buddies);

    for (const auto& pair : buddies)
    {
        const LLUUID& avatar_id = pair.first;
        if (avatar_id.isNull())
        {
            continue;
        }

        // Only invite residents that are currently online.
        if (!LLAvatarTracker::instance().isBuddyOnline(avatar_id))
        {
            continue;
        }

        // Never invite someone who is already here/nearby.
        if (isInAgentRegion(avatar_id))
        {
            continue;
        }

        ALClubInviteContactItem* item = new ALClubInviteContactItem(avatar_id);
        item->setOnCooldown(isOnCooldown(avatar_id));
        mContactList->addItem(item, LLSD(avatar_id));

        auto connection = LLAvatarNameCache::get(avatar_id,
            boost::bind(&ALFloaterClubInvite::onAvatarNameLoaded, this, _1, _2));
        mAvatarNameConnections.push_back(connection);
    }
}

void ALFloaterClubInvite::onClickRefresh()
{
    populateContacts();
}

void ALFloaterClubInvite::onClickAliases()
{
    LLFloaterReg::showInstance("aliases");
}

void ALFloaterClubInvite::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    ALClubInviteContactItem* item =
        mContactList->getTypedItemByValue<ALClubInviteContactItem>(LLSD(agent_id));
    if (item)
    {
        item->setAvatarName(avname.getDisplayName(), avname.getAccountName());
    }
}

void ALFloaterClubInvite::onObjectSearch(LLLineEditor* caller, void* /*user_data*/)
{
    if (mObjectPanel && caller)
    {
        mObjectPanel->setFilterSubString(caller->getText());
    }
}

void ALFloaterClubInvite::onObjectSelected(const std::deque<LLFolderViewItem*>& items, bool /*user_action*/)
{
    if (items.empty())
    {
        mObjectItemID.setNull();
        return;
    }
    LLFolderViewModelItemInventory* view_model =
        static_cast<LLFolderViewModelItemInventory*>(items.front()->getViewModelItem());
    if (view_model)
    {
        mObjectItemID = view_model->getUUID();
    }
}

void ALFloaterClubInvite::onClickSend()
{
    if (mSending)
    {
        return;
    }
    mSending = true;
    mStopRequested = false;
    mSendBtn->setEnabled(false);
    getChild<LLButton>("stop_btn")->setEnabled(true);

    std::vector<std::pair<LLUUID, std::string>> recipients;

    const std::string base_message = mMessageEditor->getValue().asString();
    const std::string club = mClubAlias->getValue().asString();
    const std::string slurl = mClubSlurl->getValue().asString();
    const F32 cooldown = mCooldownSpin->getValueF32();

    std::vector<LLPanel*> items;
    mContactList->getItems(items);
    for (LLPanel* panel : items)
    {
        ALClubInviteContactItem* item = dynamic_cast<ALClubInviteContactItem*>(panel);
        if (!item || !item->isEnabled()
            || !LLAvatarTracker::instance().isBuddyOnline(item->getAvatarID())
            || isOnCooldown(item->getAvatarID()))
        {
            continue;
        }
        std::string message = composeMessage(base_message, club, slurl, item->getAlias());
        recipients.emplace_back(item->getAvatarID(), message);
    }

    if (recipients.empty())
    {
        mStatusText->setText(getString("ClubInviteNoRecipients"));
        mSending = false;
        mSendBtn->setEnabled(true);
        getChild<LLButton>("stop_btn")->setEnabled(false);
        return;
    }

    LLCoros::instance().launch("clubInviteSendCoro",
        boost::bind(&ALFloaterClubInvite::sendCoro, this, recipients, cooldown));
}

void ALFloaterClubInvite::onClickStop()
{
    mStopRequested = true;
    // Invalidate the running coroutine so its finish block can't re-enable the
    // UI, and reset the UI here so Stop is immediate instead of waiting for the
    // coroutine to reach its loop-top guard.
    ++mSendGeneration;
    mSending = false;
    mSendBtn->setEnabled(true);
    getChild<LLButton>("stop_btn")->setEnabled(false);
    mStatusText->setText(getString("ClubInviteStopped"));
}

bool ALFloaterClubInvite::isOnCooldown(const LLUUID& avatar_id) const
{
    const LLSD cooldowns = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING);
    const F64 last_sent = cooldowns[avatar_id.asString()].asReal();
    if (last_sent <= 0.0)
    {
        return false;
    }
    const F32 cooldown_minutes = mResendSpin ? mResendSpin->getValueF32()
                                             : DEFAULT_CLUB_INVITE_RESEND_COOLDOWN_MINUTES;
    return (LLDate::now().secondsSinceEpoch() - last_sent) < (F64)(cooldown_minutes * 60.f);
}

bool ALFloaterClubInvite::isInAgentRegion(const LLUUID& avatar_id) const
{
    const LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region)
    {
        return false;
    }
    const LLUUID agent_region_id = agent_region->getRegionID();

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);
    for (S32 i = 0; i < (S32)avatar_ids.size(); ++i)
    {
        if (avatar_ids[i] != avatar_id)
        {
            continue;
        }
        const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(avatar_positions[i]);
        if (region && region->getRegionID() == agent_region_id)
        {
            return true;
        }
    }
    return false;
}

void ALFloaterClubInvite::onSendDelayCommit()
{
    gSavedPerAccountSettings.setF32(AL_CLUB_INVITE_SEND_DELAY_SETTING, mCooldownSpin->getValueF32());
}

void ALFloaterClubInvite::onResendCooldownCommit()
{
    gSavedPerAccountSettings.setF32(AL_CLUB_INVITE_RESEND_COOLDOWN_SETTING, mResendSpin->getValueF32());
}

void ALFloaterClubInvite::markCooldown(const LLUUID& avatar_id)
{
    LLSD cooldowns = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING);
    cooldowns[avatar_id.asString()] = LLDate::now().secondsSinceEpoch();
    gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING, cooldowns);
}

void ALFloaterClubInvite::sendCoro(const std::vector<std::pair<LLUUID, std::string>>& recipients, F32 cooldown)
{
    if (LLFloaterReg::findInstance("club_invite") != this)
    {
        return;
    }
    const S32 generation = mSendGeneration;
    S32 total = (S32)recipients.size();
    S32 sent = 0;
    for (const auto& r : recipients)
    {
        // Stop if the user requested it, the floater was closed, or a newer
        // send cycle started on this floater.
        if (LLFloaterReg::findInstance("club_invite") != this || mStopRequested
            || generation != mSendGeneration)
        {
            break;
        }

        const LLUUID& avatar_id = r.first;
        const LLUUID session_id = LLIMMgr::computeSessionID(IM_NOTHING_SPECIAL, avatar_id);

        // Send the attached object first, if any.
        if (mObjectItemID.notNull())
        {
            LLViewerInventoryItem* inventory_item = gInventory.getItem(mObjectItemID);
            if (inventory_item)
            {
                LLGiveInventory::doGiveInventoryItem(avatar_id, inventory_item, session_id);
            }
        }

        // Send the message through the standard IM pipeline (handles offline).
        LLIMModel::sendMessage(r.second, session_id, avatar_id, IM_NOTHING_SPECIAL);
        markCooldown(avatar_id);

        ++sent;
        std::string status = llformat("Sent %d of %d", sent, total);
        if (LLFloaterReg::findInstance("club_invite") == this)
        {
            mStatusText->setText(status);
        }

        if (sent < total)
        {
            // Wait in short chunks so Stop/Close is responsive during the
            // cooldown instead of blocking for the full remaining period.
            for (F32 remaining = cooldown;
                 remaining > 0.f
                 && LLFloaterReg::findInstance("club_invite") == this
                 && !mStopRequested
                 && generation == mSendGeneration;
                 remaining -= 0.5f)
            {
                llcoro::suspendUntilTimeout(llmin(remaining, 0.5f));
            }
        }
    }

    // Reset the UI only for the generation that started this send and only if
    // the floater still exists; a stale coroutine must not touch the members of
    // a destroyed floater or a newer send cycle.
    if (LLFloaterReg::findInstance("club_invite") == this && generation == mSendGeneration)
    {
        mSending = false;
        mSendBtn->setEnabled(true);
        getChild<LLButton>("stop_btn")->setEnabled(false);
        mStatusText->setText(mStopRequested ? getString("ClubInviteStopped")
                                            : getString("ClubInviteFinished"));
    }
}

std::string ALFloaterClubInvite::composeMessage(const std::string& base, const std::string& club,
                                                const std::string& slurl, const std::string& name)
{
    std::string text = base;

    auto substitute = [&](const std::string& token, const std::string& value)
    {
        if (value.empty())
        {
            // remove the token and any whitespace it leaves behind (including a single
            // adjacent space so that "hey [name]" becomes "hey")
            size_t pos = 0;
            while ((pos = text.find(token, pos)) != std::string::npos)
            {
                text.erase(pos, token.length());
                // collapse an inserted double space (token between two spaces)
                if (!text.empty() && pos < text.length() && text[pos] == ' '
                    && pos > 0 && text[pos - 1] == ' ')
                {
                    text.erase(pos, 1);
                }
            }
            LLStringUtil::trim(text);
        }
        else
        {
            size_t pos = 0;
            while ((pos = text.find(token, pos)) != std::string::npos)
            {
                text.replace(pos, token.length(), value);
                pos += value.length();
            }
        }
    };

    substitute(TOKEN_NAME, name);
    substitute(TOKEN_CLUB, club);
    substitute(TOKEN_SLURL, slurl);

    return text;
}