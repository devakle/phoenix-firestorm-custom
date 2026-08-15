/**
 * @file alfloaterfriendshere.cpp
 * @brief Floater showing friends in the same place, sorted by how long they
 *        have been there, with a one-click nearby greeting.
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alfloaterfriendshere.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcallingcard.h"
#include "llcombobox.h"
#include "lldate.h"
#include "llflatlistview.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluicolor.h"
#include "lluicolortable.h"
#include "llviewermessage.h"
#include "llworld.h"
#include "llviewerparcelmgr.h"
#include "llparcel.h"
#include "llviewerregion.h"
#include "llavatarnamecache.h"
#include "llviewercontrol.h"

// same setting the club invite floater persists into (ALClubInviteAliases)
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");
static const std::string AL_FRIENDS_HERE_CUSTOM_GREETING_SETTING("ALFriendsHereCustomGreeting");
static const std::string AL_FRIENDS_HERE_CUSTOM_GREETINGS_SETTING("ALFriendsHereCustomGreetings");
static const std::string TOKEN_NAME("[name]");
static const std::string TOKEN_ALIAS("[alias]");

static void setTooltipIfTruncated(LLTextBox* text_box, const std::string& full_text)
{
    if (!text_box)
    {
        return;
    }

    const F32 text_width = text_box->getFont() ? text_box->getFont()->getWidthF32(full_text.c_str())
                                               : 0.f;
    const F32 available_width = (F32)text_box->getRect().getWidth();
    text_box->setToolTip(text_width > available_width ? full_text : LLStringUtil::null);
}

// treat as "recently arrived" when present for less than this many seconds
static const F32 kRecentSeconds = 5.f * 60.f;

// an avatar's arrival time is only reset after it has been continuously absent
// from the agent's region for this many seconds, so a transient drop from the
// avatar list does not reset the time shown for someone still on the parcel
static const F64 kAbsentGraceSeconds = 60.0;

// an avatar missing from the agent's region for this many seconds is a real
// leave (reconnect / teleport-away): on return its arrival time is reset and,
// if it was greeted, it gets flagged for welcome-back
static const F64 kRegionResetSeconds = 8.0;

// send_chat_from_viewer is a file-local free function in llfloaterimnearbychat.cpp
// (the chat bar and gesture manager forward their text through it).
extern void send_chat_from_viewer(std::string utf8_out_text, EChatType type, S32 channel);

class ALFriendsHereItem final : public LLPanel
{
public:
    ALFriendsHereItem(const LLUUID& avatar_id)
        : LLPanel()
        , mAvatarID(avatar_id)
    {
        buildFromFile("panel_al_friends_here_item.xml");
    }

    bool postBuild() override
    {
        mNameText = getChild<LLTextBox>("contact_name");
        mTimeText = getChild<LLTextBox>("time_here");
        mAliasText = getChild<LLTextBox>("contact_alias");
        mGreetBtn = getChild<LLButton>("greet_btn");
        mGreetCombo = getChild<LLComboBox>("greet_type");
        mCustomGreetingEdit = getChild<LLLineEditor>("contact_custom_greeting");

        updateAliasDisplay();

        const LLSD greetings = gSavedPerAccountSettings.getLLSD(AL_FRIENDS_HERE_CUSTOM_GREETINGS_SETTING);
        const std::string saved_greeting = greetings[mAvatarID.asString()].asString();
        if (mCustomGreetingEdit)
        {
            mCustomGreetingEdit->setText(saved_greeting);
            mCustomGreetingEdit->setCommitCallback(boost::bind(&ALFriendsHereItem::onCustomGreetingCommit, this));
            mCustomGreetingEdit->setCommitOnFocusLost(true);
        }

        mGreetBtn->setClickedCallback(boost::bind(&ALFriendsHereItem::onGreet, this));
        getChild<LLButton>("contact_edit_btn")->setClickedCallback(boost::bind(&ALFriendsHereItem::onClickEditAlias, this));
        return TRUE;
    }

    const LLUUID& getAvatarID() const { return mAvatarID; }

    void setAvatarName(const std::string& display_name, const std::string& account_name)
    {
        mAvatarName = display_name;
        mAccountName = account_name;
        updateNameText();
    }

    void setArrivalTime(F64 arrival_epoch)
    {
        mArrivalTime = arrival_epoch;
        const F64 secs = LLDate::now().secondsSinceEpoch() - mArrivalTime;
        const F32 mins = (F32)(secs / 60.0);
        std::string time_label;
        if (secs < 60.0)
        {
            time_label = llformat("~%.0f s", secs);
        }
        else if (mins < 60.f)
        {
            time_label = llformat("~%.0f min", mins);
        }
        else
        {
            time_label = llformat("~%.0f h", mins / 60.f);
        }
        mTimeText->setText(time_label);
        updateNameColor();
    }

    std::string getGreetingName() const
    {
        const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
        const std::string alias = aliases[mAvatarID.asString()].asString();
        return alias.empty() ? mAvatarName : alias;
    }

    bool isRecentArrival() const
    {
        const F64 now = LLDate::now().secondsSinceEpoch();
        return (now - mArrivalTime) < kRecentSeconds;
    }

    void updateNameColor()
    {
        LLUIColor color = LLUIColorTable::instance().getColor("White");
        ALFloaterFriendsHere* friends_here = LLFloaterReg::findTypedInstance<ALFloaterFriendsHere>("friends_here");
        if (friends_here)
        {
            if (friends_here->needsWelcomeBack(mAvatarID))
            {
                color = LLUIColorTable::instance().getColor("Yellow");
            }
            else if (friends_here->hasBeenGreeted(mAvatarID))
            {
                color = LLUIColorTable::instance().getColor("White");
            }
            else if (isRecentArrival())
            {
                color = LLUIColorTable::instance().getColor("Green");
            }
        }
        else if (isRecentArrival())
        {
            color = LLUIColorTable::instance().getColor("Green");
        }
        mNameText->setColor(color);
    }

private:
    void onGreet()
    {
        const std::string name = getGreetingName();
        std::string message;
        const std::string mode = mGreetCombo ? mGreetCombo->getValue().asString() : "greet";

        std::string resident_custom = mCustomGreetingEdit ? mCustomGreetingEdit->getText() : "";
        if (resident_custom.empty())
        {
            const LLSD greetings = gSavedPerAccountSettings.getLLSD(AL_FRIENDS_HERE_CUSTOM_GREETINGS_SETTING);
            resident_custom = greetings[mAvatarID.asString()].asString();
        }

        if (mode == "wb")
        {
            message = "welcome back " + name + "! :)";
        }
        else if (!resident_custom.empty())
        {
            message = resident_custom;
            auto substitute = [&message](const std::string& token, const std::string& value)
            {
                size_t pos = 0;
                while ((pos = message.find(token, pos)) != std::string::npos)
                {
                    message.replace(pos, token.length(), value);
                    pos += value.length();
                }
            };
            substitute(TOKEN_NAME, name);
            substitute(TOKEN_ALIAS, name);
        }
        else
        {
            const std::string custom = gSavedPerAccountSettings.getString(AL_FRIENDS_HERE_CUSTOM_GREETING_SETTING);
            if (!custom.empty())
            {
                // Custom greeting template overrides the presets. [name]/[alias]
                // are replaced with the contact's greeting name.
                message = custom;
                auto substitute = [&message](const std::string& token, const std::string& value)
                {
                    size_t pos = 0;
                    while ((pos = message.find(token, pos)) != std::string::npos)
                    {
                        message.replace(pos, token.length(), value);
                        pos += value.length();
                    }
                };
                substitute(TOKEN_NAME, name);
                substitute(TOKEN_ALIAS, name);
            }
            else
            {
                std::string greeting = (ll_rand(2) == 0) ? "hey" : "hello";
                message = greeting + " " + name + "! :)";
            }
        }
        send_chat_from_viewer(message, CHAT_TYPE_NORMAL, 0);

        ALFloaterFriendsHere* friends_here = LLFloaterReg::findTypedInstance<ALFloaterFriendsHere>("friends_here");
        if (friends_here)
        {
            if (mode == "wb")
            {
                friends_here->onWelcomeBackSent(mAvatarID);
            }
            else
            {
                friends_here->onGreetSent(mAvatarID);
            }
        }
        updateNameColor();
    }

    void onClickEditAlias()
    {
        LLFloaterReg::showInstance("aliases", LLSD().with("avatar_id", mAvatarID));
    }

    void onCustomGreetingCommit()
    {
        if (!mCustomGreetingEdit) return;
        LLSD greetings = gSavedPerAccountSettings.getLLSD(AL_FRIENDS_HERE_CUSTOM_GREETINGS_SETTING);
        greetings[mAvatarID.asString()] = mCustomGreetingEdit->getText();
        gSavedPerAccountSettings.setLLSD(AL_FRIENDS_HERE_CUSTOM_GREETINGS_SETTING, greetings);
    }

    void updateNameText()
    {
        std::string label = mAvatarName;
        if (!mAvatarName.empty() && !mAccountName.empty())
        {
            label += " (" + mAccountName + ")";
        }
        mNameText->setText(label);
        setTooltipIfTruncated(mNameText, label);
        updateAliasDisplay();
    }

    void updateAliasDisplay()
    {
        if (!mAliasText)
        {
            return;
        }
        const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
        const std::string alias = aliases[mAvatarID.asString()].asString();
        const std::string label = alias.empty() ? mAvatarName : alias;
        mAliasText->setText(label);
        setTooltipIfTruncated(mAliasText, label);
    }

    LLTextBox*    mNameText = nullptr;
    LLTextBox*    mTimeText = nullptr;
    LLTextBox*    mAliasText = nullptr;
    LLButton*     mGreetBtn = nullptr;
    LLComboBox*   mGreetCombo = nullptr;
    LLLineEditor* mCustomGreetingEdit = nullptr;

    LLUUID      mAvatarID;
    std::string mAvatarName;
    std::string mAccountName;
    F64         mArrivalTime = 0.0;
};

ALFloaterFriendsHere::ALFloaterFriendsHere(const LLSD& key)
    : LLFloater(key)
    , LLEventTimer(2.f)
{}

ALFloaterFriendsHere::~ALFloaterFriendsHere()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
}

bool ALFloaterFriendsHere::postBuild()
{
    mFriendList = getChild<LLFlatListView>("friend_list");
    mStatusText = getChild<LLTextBox>("status_text");
    mRefreshBtn = getChild<LLButton>("refresh_btn");
    mRefreshBtn->setClickedCallback(boost::bind(&ALFloaterFriendsHere::refreshFriendsList, this));
    getChild<LLButton>("hi_all_btn")->setClickedCallback(boost::bind(&ALFloaterFriendsHere::onClickHelloAll, this));
    getChild<LLButton>("aliases_btn")->setClickedCallback(boost::bind(&ALFloaterFriendsHere::onClickAliases, this));
    mCustomGreetingEdit = getChild<LLLineEditor>("custom_greeting");
    mCustomGreetingEdit->setText(gSavedPerAccountSettings.getString(AL_FRIENDS_HERE_CUSTOM_GREETING_SETTING));
    mCustomGreetingEdit->setCommitCallback(boost::bind(&ALFloaterFriendsHere::onCustomGreetingCommit, this));
    mCustomGreetingEdit->setCommitOnFocusLost(true);
    mStatusText->setText(getString("FriendsHereStatus"));
    return TRUE;
}

void ALFloaterFriendsHere::onOpen(const LLSD& key)
{
    mEventTimer.start();
    // welcome-back tracking is scoped to a floater session: reopening should not
    // re-flag avatars from a previous session. Arrival times keep counting.
    mLeftAfterGreeting.clear();
    mNeedsWelcomeBack.clear();
    mParcelAbsentSince.clear();
    refreshFriendsList();
}

void ALFloaterFriendsHere::onClose(bool app_quitting)
{
    mEventTimer.stop();
}

bool ALFloaterFriendsHere::tick()
{
    refreshFriendsList();
    return FALSE; // keep ticking
}

void ALFloaterFriendsHere::refreshFriendsList()
{
    const LLViewerRegion* agent_region = gAgent.getRegion();
    if (!agent_region)
    {
        return;
    }
    const LLUUID scope_region_id = agent_region->getRegionID();

    S32 current_parcel_id = -1;
    LLParcel* agent_parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
    if (agent_parcel)
    {
        current_parcel_id = agent_parcel->getLocalID();
    }

    if (mCurrentParcelID != current_parcel_id || mCurrentRegionID != scope_region_id)
    {
        mCurrentParcelID = current_parcel_id;
        mCurrentRegionID = scope_region_id;
        mGreetedAvatars.clear();
        mLeftAfterGreeting.clear();
        mNeedsWelcomeBack.clear();
        mAbsentSince.clear();
        mParcelAbsentSince.clear();
    }

    std::vector<LLUUID> present;
    arrival_time_map_t arrivals_now;

    std::set<LLUUID> previously_present;
    std::vector<LLPanel*> prev_panels;
    mFriendList->getItems(prev_panels);
    for (LLPanel* panel : prev_panels)
    {
        ALFriendsHereItem* item = dynamic_cast<ALFriendsHereItem*>(panel);
        if (item)
        {
            previously_present.insert(item->getAvatarID());
        }
    }

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);

    // avatars currently known to be in the agent's region; used below so arrival
    // times survive a buddy stepping off the parcel but staying in the region
    std::set<LLUUID> avatars_in_region;
    for (S32 i = 0; i < (S32)avatar_ids.size(); ++i)
    {
        const LLVector3d& global_pos = avatar_positions[i];
        const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
        if (region && region->getRegionID() == scope_region_id)
        {
            avatars_in_region.insert(avatar_ids[i]);
        }
    }

    for (S32 i = 0; i < (S32)avatar_ids.size(); ++i)
    {
        const LLUUID& id = avatar_ids[i];
        const LLVector3d& global_pos = avatar_positions[i];
        const LLViewerRegion* region = LLWorld::getInstance()->getRegionFromPosGlobal(global_pos);
        if (!region || region->getRegionID() != scope_region_id)
        {
            continue;
        }
        if (!LLAvatarTracker::instance().isBuddy(id))
        {
            continue;
        }
        if (!LLViewerParcelMgr::getInstance()->inAgentParcel(global_pos))
        {
            continue;
        }

        present.push_back(id);
        F64 arrival = 0.0;
        arrival_time_map_t::const_iterator it = mArrivalTimes.find(id);
        if (it != mArrivalTimes.end())
        {
            arrival = it->second;
        }
        else
        {
            arrival = LLDate::now().secondsSinceEpoch();
        }
        arrivals_now[id] = arrival;
        mParcelAbsentSince.erase(id);

        if (previously_present.find(id) == previously_present.end())
        {
            if (mLeftAfterGreeting.count(id))
            {
                mNeedsWelcomeBack.insert(id);
                mLeftAfterGreeting.erase(id);
            }
        }
    }

    // Mark those who left the parcel but kept an eye on the region, only after a
    // continuous absence of more than the grace period so a transient blip does
    // not falsely trigger a welcome-back when they return
    const F64 now_present = LLDate::now().secondsSinceEpoch();
    for (const LLUUID& id : previously_present)
    {
        if (arrivals_now.find(id) != arrivals_now.end())
        {
            mParcelAbsentSince.erase(id);
            continue;
        }
        if (avatars_in_region.find(id) == avatars_in_region.end())
        {
            // left the region entirely: handled by the region loop below
            mParcelAbsentSince.erase(id);
            continue;
        }
        if (!mGreetedAvatars.count(id))
        {
            mParcelAbsentSince.erase(id);
            continue;
        }
        arrival_time_map_t::const_iterator dep = mParcelAbsentSince.find(id);
        if (dep == mParcelAbsentSince.end())
        {
            mParcelAbsentSince[id] = now_present;
        }
        else if (now_present - dep->second >= kAbsentGraceSeconds)
        {
            mLeftAfterGreeting.insert(id);
            mNeedsWelcomeBack.erase(id);
            mParcelAbsentSince.erase(id);
        }
    }

    // Reconcile with the agent's region. An avatar missing from the region is a
    // real leave (reconnect / teleport-away), unlike a short blotch in the avatar
    // list: flag a welcome-back for when it returns and, once it has been away
    // for a few seconds, reset its arrival time so the return counts as a fresh
    // arrival. The welcome-back flag is intentionally kept across the absence.
    const F64 now = LLDate::now().secondsSinceEpoch();
    for (arrival_time_map_t::const_iterator it = mArrivalTimes.begin(); it != mArrivalTimes.end();)
    {
        const LLUUID& id = it->first;
        if (avatars_in_region.find(id) != avatars_in_region.end())
        {
            mAbsentSince.erase(id);
            ++it;
            continue;
        }
        if (mAbsentSince.find(id) == mAbsentSince.end())
        {
            mAbsentSince[id] = now;
            if (mGreetedAvatars.count(id) || mNeedsWelcomeBack.count(id))
            {
                mLeftAfterGreeting.insert(id);
                mGreetedAvatars.erase(id);
                mNeedsWelcomeBack.erase(id);
            }
            ++it;
            continue;
        }
        if (now - mAbsentSince[id] >= kRegionResetSeconds)
        {
            mAbsentSince.erase(id);
            mParcelAbsentSince.erase(id);
            mArrivalTimes.erase(it++);
        }
        else
        {
            ++it;
        }
    }
    for (const auto& pair : arrivals_now)
    {
        mArrivalTimes[pair.first] = pair.second;
    }

    // order present ids by arrival time, oldest first
    typedef std::multimap<F64, LLUUID> sort_map_t;
    sort_map_t sorted;
    for (const auto& id : present)
    {
        sorted.insert(std::make_pair(mArrivalTimes[id], id));
    }

    // reconcile instead of rebuilding: the alias line editors must survive a refresh
    std::map<LLUUID, ALFriendsHereItem*> existing_items;
    std::vector<LLPanel*> current_items;
    mFriendList->getItems(current_items);
    for (LLPanel* panel : current_items)
    {
        ALFriendsHereItem* item = dynamic_cast<ALFriendsHereItem*>(panel);
        if (item)
        {
            existing_items[item->getAvatarID()] = item;
        }
    }

    // drop rows for avatars that are no longer present
    for (auto& pair : existing_items)
    {
        if (arrivals_now.find(pair.first) == arrivals_now.end())
        {
            mFriendList->removeItem(pair.second);
        }
    }

    std::vector<LLUUID> present_sorted;
    for (sort_map_t::const_iterator it = sorted.begin(); it != sorted.end(); ++it)
    {
        present_sorted.push_back(it->second);
    }

    // add rows for newly present avatars, in arrival order; reuse surviving rows so
    // the alias line editors keep focus and content across refreshes
    for (const LLUUID& id : present_sorted)
    {
        ALFriendsHereItem* item = nullptr;
        auto it = existing_items.find(id);
        if (it != existing_items.end() && arrivals_now.find(id) != arrivals_now.end())
        {
            item = it->second;
        }
        if (!item)
        {
            item = new ALFriendsHereItem(id);
            mFriendList->addItem(item, LLSD(id));

            auto conn = LLAvatarNameCache::get(id,
                boost::bind(&ALFloaterFriendsHere::onAvatarNameLoaded, this, _1, _2));
            mAvatarNameConnections.push_back(conn);
        }
        item->setArrivalTime(mArrivalTimes[id]);
    }

    mStatusText->setText(present_sorted.empty() ? getString("FriendsHereNoFriends")
                                                : getString("FriendsHereStatus"));
}

void ALFloaterFriendsHere::onClickAliases()
{
    LLFloaterReg::showInstance("aliases");
}

void ALFloaterFriendsHere::onClickHelloAll()
{
    send_chat_from_viewer("hi all :)", CHAT_TYPE_NORMAL, 0);

    std::vector<LLPanel*> items;
    mFriendList->getItems(items);
    for (LLPanel* panel : items)
    {
        ALFriendsHereItem* item = dynamic_cast<ALFriendsHereItem*>(panel);
        if (item)
        {
            onGreetSent(item->getAvatarID());
            item->updateNameColor();
        }
    }
}

void ALFloaterFriendsHere::refreshForAliasChange()
{
    refreshFriendsList();
}

void ALFloaterFriendsHere::onCustomGreetingCommit()
{
    gSavedPerAccountSettings.setString(AL_FRIENDS_HERE_CUSTOM_GREETING_SETTING,
                                       mCustomGreetingEdit->getText());
}

void ALFloaterFriendsHere::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    std::vector<LLPanel*> items;
    mFriendList->getItems(items);
    for (LLPanel* p : items)
    {
        ALFriendsHereItem* item = dynamic_cast<ALFriendsHereItem*>(p);
        if (item && item->getAvatarID() == agent_id)
        {
            item->setAvatarName(avname.getDisplayName(), avname.getAccountName());
            break;
        }
    }
}
