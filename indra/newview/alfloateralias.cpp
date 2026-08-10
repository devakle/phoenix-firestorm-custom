/**
 * @file alfloateralias.cpp
 * @brief Central floater to manage the alias shared by the club invitation and
 *        friends here floaters.
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alfloateralias.h"

#include "llbutton.h"
#include "llcallingcard.h"
#include "llflatlistview.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "lltextbox.h"
#include "llavatarnamecache.h"
#include "llviewercontrol.h"

// same setting the club invite and friends here floaters persist into
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");

class ALFloaterAliasesItem final : public LLPanel
{
public:
    ALFloaterAliasesItem(const LLUUID& avatar_id)
        : LLPanel()
        , mAvatarID(avatar_id)
    {
        buildFromFile("panel_al_aliases_contact.xml");
    }

    bool postBuild() override
    {
        mNameText = getChild<LLTextBox>("contact_name");
        mAliasEdit = getChild<LLLineEditor>("contact_alias");

        const LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
        const std::string saved_alias = aliases[mAvatarID.asString()].asString();
        mAliasEdit->setText(saved_alias);

        mAliasEdit->setCommitCallback(boost::bind(&ALFloaterAliasesItem::onAliasEdited, this));
        mAliasEdit->setCommitOnFocusLost(true);
        return true;
    }

    const LLUUID& getAvatarID() const { return mAvatarID; }

    void setAvatarName(const std::string& display_name, const std::string& account_name)
    {
        std::string label = display_name;
        if (!display_name.empty() && !account_name.empty())
        {
            label += " (" + account_name + ")";
        }
        mNameText->setText(label);
    }

    void focusAliasEditor()
    {
        mAliasEdit->setFocus(true);
        mAliasEdit->selectAll();
    }

private:
    void onAliasEdited()
    {
        LLSD aliases = gSavedPerAccountSettings.getLLSD(AL_CLUB_INVITE_ALIASES_SETTING);
        aliases[mAvatarID.asString()] = mAliasEdit->getText();
        gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_ALIASES_SETTING, aliases);
    }

    LLUUID      mAvatarID;
    LLTextBox*  mNameText = nullptr;
    LLLineEditor* mAliasEdit = nullptr;
};

ALFloaterAliases::ALFloaterAliases(const LLSD& key)
    : LLFloater(key)
{}

ALFloaterAliases::~ALFloaterAliases()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
}

bool ALFloaterAliases::postBuild()
{
    mAliasList = getChild<LLFlatListView>("alias_list");
    mStatusText = getChild<LLTextBox>("status_text");

    mRefreshBtn = getChild<LLButton>("refresh_btn");
    mRefreshBtn->setClickedCallback(boost::bind(&ALFloaterAliases::onClickRefresh, this));

    return true;
}

void ALFloaterAliases::onOpen(const LLSD& key)
{
    populateList();
    if (key.has("avatar_id"))
    {
        focusContact(key["avatar_id"].asUUID());
    }
}

void ALFloaterAliases::populateList()
{
    for (auto& conn : mAvatarNameConnections)
    {
        conn.disconnect();
    }
    mAvatarNameConnections.clear();
    mAliasList->clear();

    LLAvatarTracker::buddy_map_t buddies;
    LLAvatarTracker::instance().copyBuddyList(buddies);

    S32 count = 0;
    for (const auto& pair : buddies)
    {
        const LLUUID& avatar_id = pair.first;
        if (avatar_id.isNull())
        {
            continue;
        }

        ALFloaterAliasesItem* item = new ALFloaterAliasesItem(avatar_id);
        mAliasList->addItem(item, LLSD(avatar_id));

        auto connection = LLAvatarNameCache::get(avatar_id,
            boost::bind(&ALFloaterAliases::onAvatarNameLoaded, this, _1, _2));
        mAvatarNameConnections.push_back(connection);
        ++count;
    }

    if (mStatusText)
    {
        mStatusText->setText(llformat("%d contacts", count));
    }
}

void ALFloaterAliases::onClickRefresh()
{
    populateList();
}

void ALFloaterAliases::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    ALFloaterAliasesItem* item =
        mAliasList->getTypedItemByValue<ALFloaterAliasesItem>(LLSD(agent_id));
    if (item)
    {
        item->setAvatarName(avname.getDisplayName(), avname.getAccountName());
    }
}

void ALFloaterAliases::focusContact(const LLUUID& avatar_id)
{
    ALFloaterAliasesItem* item =
        mAliasList->getTypedItemByValue<ALFloaterAliasesItem>(LLSD(avatar_id));
    if (item)
    {
        mAliasList->scrollToShowRect(item->getRect());
        item->focusAliasEditor();
    }
}