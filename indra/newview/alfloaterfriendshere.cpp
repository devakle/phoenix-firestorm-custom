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
#include "llviewerregion.h"
#include "llavatarnamecache.h"
#include "llviewercontrol.h"

// same setting the club invite floater persists into (ALClubInviteAliases)
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");
static const std::string AL_FRIENDS_HERE_CUSTOM_GREETING_SETTING("ALFriendsHereCustomGreeting");
static const std::string TOKEN_NAME("[name]");
static const std::string TOKEN_ALIAS("[alias]");

// treat as "recently arrived" when present for less than this many seconds
static const F32 kRecentSeconds = 5.f * 60.f;

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
        mAliasEdit = getChild<LLLineEditor>("contact_alias");
        mGreetBtn = getChild<LLButton>("greet_btn");
        mGreetCombo = getChild<LLComboBox>("greet_type");

        mAliasEdit->setCommitCallback(boost::bind(&ALFriendsHereItem::onAliasEdited, this));
        mAliasEdit->setCommitOnFocusLost(true);
        mGreetBtn->setClickedCallback(boost::bind(&ALFriendsHereItem::onGreet, this));
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
        const std::string alias = mAliasEdit->getText();
        return alias.empty() ? mAvatarName : alias;
    }

    void setAlias(const std::string& alias)
    {
        mAliasEdit->setText(alias);
        updateNameText();
    }

    bool isRecentArrival() const
    {
        const F64 now = LLDate::now().secondsSinceEpoch();
        return (now - mArrivalTime) < kRecentSeconds;
    }

private:
    void onGreet()
    {
        const std::string name = getGreetingName();
        std::string message;
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
            const std::string mode = mGreetCombo ? mGreetCombo->getValue().asString() : "greet";
            if (mode == "wb")
            {
                message = "welcome back " + name + "! :)";
            }
            else
            {
                std::string greeting = (ll_rand(2) == 0) ? "hey" : "hello";
                message = greeting + " " + name + "! :)";
            }
        }
        send_chat_from_viewer(message, CHAT_TYPE_NORMAL, 0);
    }

    void onAliasEdited()
    {
        LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
        aliases[mAvatarID.asString()] = mAliasEdit->getText();
        gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_ALIASES_SETTING, aliases);
        updateNameText();
    }

    void updateNameText()
    {
        std::string label = mAliasEdit ? mAliasEdit->getText() : std::string();
        if (label.empty())
        {
            label = mAvatarName;
            if (!mAvatarName.empty() && !mAccountName.empty())
            {
                label += " (" + mAccountName + ")";
            }
        }
        mNameText->setText(label);
    }

    void updateNameColor()
    {
        const LLUIColor color = isRecentArrival()
            ? LLUIColorTable::instance().getColor("Green")
            : LLUIColorTable::instance().getColor("White");
        mNameText->setColor(color);
    }

    LLTextBox*    mNameText = nullptr;
    LLTextBox*    mTimeText = nullptr;
    LLLineEditor* mAliasEdit = nullptr;
    LLButton*     mGreetBtn = nullptr;
    LLComboBox*   mGreetCombo = nullptr;

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

    std::vector<LLUUID> present;
    arrival_time_map_t arrivals_now;

    uuid_vec_t avatar_ids;
    std::vector<LLVector3d> avatar_positions;
    LLWorld::getInstance()->getAvatars(&avatar_ids, &avatar_positions);
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
    }

    // drop ids that are no longer present, keep arrival times for continuing ones
    for (arrival_time_map_t::const_iterator it = mArrivalTimes.begin(); it != mArrivalTimes.end();)
    {
        if (arrivals_now.find(it->first) == arrivals_now.end())
        {
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

            const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
            const std::string alias = aliases[id.asString()].asString();
            item->setAlias(alias);

            auto conn = LLAvatarNameCache::get(id,
                boost::bind(&ALFloaterFriendsHere::onAvatarNameLoaded, this, _1, _2));
            mAvatarNameConnections.push_back(conn);
        }
        item->setArrivalTime(mArrivalTimes[id]);
    }

    mStatusText->setText(present_sorted.empty() ? getString("FriendsHereNoFriends")
                                                : getString("FriendsHereStatus"));
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
