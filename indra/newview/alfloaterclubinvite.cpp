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

static const std::string TOKEN_CLUB("[club]");
static const std::string TOKEN_SLURL("[slurl]");
static const std::string TOKEN_NAME("[name]");
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");
static const std::string AL_CLUB_INVITE_COOLDOWN_SETTING("ALClubInviteCooldowns");
static const F32           CLUB_INVITE_COOLDOWN_SECONDS(30.f * 60.f);

class ALClubInviteContactItem final : public LLPanel
{
public:
    ALClubInviteContactItem(const LLUUID& avatar_id);

    bool postBuild() override;
    bool handleKeyHere(KEY key, MASK mask) override;

    const LLUUID& getAvatarID() const { return mAvatarID; }
    bool isEnabled() const;
    void setAvatarName(const std::string& avatar_name);
    std::string getAlias() const;
    void setOnCooldown(bool cooldown);

private:
    void onAliasEdited();
    bool focusSiblingAlias(bool forward);

    LLUUID      mAvatarID;
    LLCheckBoxCtrl* mEnabledCheck = nullptr;
    LLTextBox*  mNameText = nullptr;
    LLLineEditor* mAliasEdit = nullptr;
    bool        mOnCooldown = false;
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
    mAliasEdit = getChild<LLLineEditor>("contact_alias");
    setValue(LLSD(mAvatarID));

    const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
    const std::string saved_alias = aliases[mAvatarID.asString()].asString();
    if (!saved_alias.empty())
    {
        mAliasEdit->setText(saved_alias);
    }

    mAliasEdit->setCommitCallback(boost::bind(&ALClubInviteContactItem::onAliasEdited, this));
    mAliasEdit->setCommitOnFocusLost(true);
    return true;
}

bool ALClubInviteContactItem::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_TAB && mAliasEdit && mAliasEdit->hasFocus())
    {
        const bool forward = (mask == MASK_NONE);
        if (focusSiblingAlias(forward))
        {
            return true;
        }
    }
    return LLPanel::handleKeyHere(key, mask);
}

bool ALClubInviteContactItem::focusSiblingAlias(bool forward)
{
    LLFlatListView* flat_list = nullptr;
    for (LLView* parent = getParent(); parent; parent = parent->getParent())
    {
        flat_list = dynamic_cast<LLFlatListView*>(parent);
        if (flat_list)
        {
            break;
        }
    }
    if (!flat_list)
    {
        return false;
    }

    std::vector<LLPanel*> items;
    flat_list->getItems(items);

    S32 current = -1;
    for (S32 i = 0; i < (S32)items.size(); ++i)
    {
        if (items[i] == this)
        {
            current = i;
            break;
        }
    }
    if (current < 0)
    {
        return false;
    }

    const S32 count = (S32)items.size();
    S32 next = forward ? current + 1 : current - 1;
    // skip the current item if it is the only one; otherwise wrap around
    if (next >= count)
    {
        next = 0;
    }
    else if (next < 0)
    {
        next = count - 1;
    }
    if (next == current)
    {
        return false;
    }

    ALClubInviteContactItem* target_item = dynamic_cast<ALClubInviteContactItem*>(items[next]);
    if (!(target_item && target_item->mAliasEdit))
    {
        return false;
    }

    // make sure the incoming item is visible before stealing focus
    flat_list->scrollToShowRect(target_item->getRect());
    target_item->mAliasEdit->setFocus(true);
    target_item->mAliasEdit->selectAll();
    return true;
}

void ALClubInviteContactItem::onAliasEdited()
{
    LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
    aliases[mAvatarID.asString()] = mAliasEdit->getText();
    gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_ALIASES_SETTING, aliases);
}

bool ALClubInviteContactItem::isEnabled() const
{
    return !mOnCooldown && mEnabledCheck && mEnabledCheck->get();
}

void ALClubInviteContactItem::setAvatarName(const std::string& avatar_name)
{
    mNameText->setText(avatar_name);
    if (mAliasEdit->getText().empty())
    {
        mAliasEdit->setText(avatar_name);
    }
}

std::string ALClubInviteContactItem::getAlias() const
{
    return mAliasEdit ? mAliasEdit->getText() : std::string();
}

void ALClubInviteContactItem::setOnCooldown(bool cooldown)
{
    mOnCooldown = cooldown;
    if (mEnabledCheck)
    {
        mEnabledCheck->setEnabled(!cooldown);
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
    mContactList = getChild<LLFlatListView>("contact_list");
    mStatusText = getChild<LLTextBox>("status_text");
    mObjectPanel = getChild<LLAssetFilteredInventoryPanel>("object_panel");

    mSendBtn = getChild<LLButton>("send_btn");
    mSendBtn->setCommitCallback(boost::bind(&ALFloaterClubInvite::onClickSend, this));
    getChild<LLButton>("stop_btn")->setCommitCallback(boost::bind(&ALFloaterClubInvite::onClickStop, this));
    getChild<LLButton>("stop_btn")->setEnabled(false);

    if (mObjectPanel)
    {
        mObjectPanel->setSelectCallback(boost::bind(&ALFloaterClubInvite::onObjectSelected, this, _1, _2));
    }

    return true;
}

void ALFloaterClubInvite::onOpen(const LLSD&)
{
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

        ALClubInviteContactItem* item = new ALClubInviteContactItem(avatar_id);
        item->setOnCooldown(isOnCooldown(avatar_id));
        mContactList->addItem(item, LLSD(avatar_id));

        auto connection = LLAvatarNameCache::get(avatar_id,
            boost::bind(&ALFloaterClubInvite::onAvatarNameLoaded, this, _1, _2));
        mAvatarNameConnections.push_back(connection);
    }
}

void ALFloaterClubInvite::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    ALClubInviteContactItem* item =
        mContactList->getTypedItemByValue<ALClubInviteContactItem>(LLSD(agent_id));
    if (item)
    {
        item->setAvatarName(avname.getDisplayName());
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
}

bool ALFloaterClubInvite::isOnCooldown(const LLUUID& avatar_id) const
{
    const LLSD cooldowns = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING);
    const F64 last_sent = cooldowns[avatar_id.asString()].asReal();
    if (last_sent <= 0.0)
    {
        return false;
    }
    return (LLDate::now().secondsSinceEpoch() - last_sent) < CLUB_INVITE_COOLDOWN_SECONDS;
}

void ALFloaterClubInvite::markCooldown(const LLUUID& avatar_id)
{
    LLSD cooldowns = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING);
    cooldowns[avatar_id.asString()] = LLDate::now().secondsSinceEpoch();
    gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_COOLDOWN_SETTING, cooldowns);
}

void ALFloaterClubInvite::sendCoro(const std::vector<std::pair<LLUUID, std::string>>& recipients, F32 cooldown)
{
    S32 total = (S32)recipients.size();
    S32 sent = 0;
    for (const auto& r : recipients)
    {
        // Stop if the user requested it or the floater was closed.
        if (LLFloaterReg::findInstance("club_invite") != this || mStopRequested)
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
            llcoro::suspendUntilTimeout(cooldown);
        }
    }

    if (LLFloaterReg::findInstance("club_invite") != this)
    {
        return; // floater destroyed; do not touch members
    }

    mSending = false;
    mSendBtn->setEnabled(true);
    getChild<LLButton>("stop_btn")->setEnabled(false);
    mStatusText->setText(mStopRequested ? getString("ClubInviteStopped")
                                        : getString("ClubInviteFinished"));
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