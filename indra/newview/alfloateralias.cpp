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
#include "alfloaterclubinvite.h"
#include "alfloaterfriendshere.h"
#include "llviewercontrol.h"
#include "llfiltereditor.h"

// same setting the club invite and friends here floaters persist into
static const std::string AL_CLUB_INVITE_ALIASES_SETTING("ALClubInviteAliases");

#include "lluictrlfactory.h"

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

class ALFloaterAliasesItem;

class ALAliasLineEditor : public LLLineEditor
{
public:
    struct Params : public LLInitParam::Block<Params, LLLineEditor::Params>
    {
        Params() {}
    };

    ALAliasLineEditor(const Params& p)
        : LLLineEditor(p)
    {}

    bool handleKeyHere(KEY key, MASK mask) override;
};

static LLDefaultChildRegistry::Register<ALAliasLineEditor> r_al_alias_line_editor("al_alias_line_editor");

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
        return true;
    }


    const LLUUID& getAvatarID() const { return mAvatarID; }

    void setAvatarName(const std::string& display_name, const std::string& account_name)
    {
        mDisplayName = display_name;
        mAccountName = account_name;

        std::string label = display_name;
        if (!display_name.empty() && !account_name.empty())
        {
            label += " (" + account_name + ")";
        }
        mNameText->setText(label);
        setTooltipIfTruncated(mNameText, label);
    }

    bool matchesFilter(const std::string& query) const
    {
        if (query.empty())
        {
            return true;
        }

        std::string query_lower = query;
        LLStringUtil::toLower(query_lower);

        std::string dn_lower = mDisplayName;
        LLStringUtil::toLower(dn_lower);
        if (dn_lower.find(query_lower) != std::string::npos)
        {
            return true;
        }

        std::string an_lower = mAccountName;
        LLStringUtil::toLower(an_lower);
        if (an_lower.find(query_lower) != std::string::npos)
        {
            return true;
        }

        std::string alias_lower = getAliasText();
        LLStringUtil::toLower(alias_lower);
        if (alias_lower.find(query_lower) != std::string::npos)
        {
            return true;
        }

        return false;
    }

    void focusAliasEditor()
    {
        mAliasEdit->setFocus(true);
        mAliasEdit->selectAll();
    }

    std::string getAliasText() const
    {
        return mAliasEdit ? mAliasEdit->getText() : LLStringUtil::null;
    }

    LLUUID      mAvatarID;
    LLTextBox*  mNameText = nullptr;
    LLLineEditor* mAliasEdit = nullptr;
    std::string mDisplayName;
    std::string mAccountName;
};

bool ALAliasLineEditor::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_TAB)
    {
        LLView* current = getParent();
        ALFloaterAliasesItem* item = nullptr;
        while (current)
        {
            item = dynamic_cast<ALFloaterAliasesItem*>(current);
            if (item) break;
            current = current->getParent();
        }

        if (item)
        {
            LLView* parent = item->getParent();
            LLFlatListView* list_view = nullptr;
            while (parent)
            {
                list_view = dynamic_cast<LLFlatListView*>(parent);
                if (list_view) break;
                parent = parent->getParent();
            }

            if (list_view)
            {
                std::vector<LLPanel*> items;
                list_view->getItems(items);

                auto it = std::find(items.begin(), items.end(), item);
                if (it != items.end())
                {
                    size_t idx = std::distance(items.begin(), it);
                    size_t next_idx = idx;

                    if (mask & MASK_SHIFT)
                    {
                        if (idx > 0)
                        {
                            next_idx = idx - 1;
                        }
                        else if (!items.empty())
                        {
                            next_idx = items.size() - 1;
                        }
                    }
                    else
                    {
                        if (idx + 1 < items.size())
                        {
                            next_idx = idx + 1;
                        }
                        else
                        {
                            next_idx = 0;
                        }
                    }

                    if (next_idx != idx && next_idx < items.size())
                    {
                        ALFloaterAliasesItem* next_item = dynamic_cast<ALFloaterAliasesItem*>(items[next_idx]);
                        if (next_item)
                        {
                            list_view->scrollToShowRect(next_item->getRect());
                            next_item->focusAliasEditor();
                            return true;
                        }
                    }
                }
            }
        }
    }

    return LLLineEditor::handleKeyHere(key, mask);
}

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
    mSaveBtn = getChild<LLButton>("save_btn");
    mSaveBtn->setClickedCallback(boost::bind(&ALFloaterAliases::onClickSave, this));

    mFilterEditor = getChild<LLFilterEditor>("alias_filter");
    mFilterEditor->setCommitCallback(boost::bind(&ALFloaterAliases::onFilterEdit, this, _2));

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

    if (mFilterEditor)
    {
        onFilterEdit(mFilterEditor->getText());
    }
}

void ALFloaterAliases::onClickRefresh()
{
    populateList();
}

void ALFloaterAliases::onClickSave()
{
    LLSD aliases;

    std::vector<LLPanel*> items;
    mAliasList->getItems(items);
    for (LLPanel* panel : items)
    {
        ALFloaterAliasesItem* item = dynamic_cast<ALFloaterAliasesItem*>(panel);
        if (!item)
        {
            continue;
        }

        aliases[item->getAvatarID().asString()] = item->getAliasText();
    }

    gSavedPerAccountSettings.setLLSD(AL_CLUB_INVITE_ALIASES_SETTING, aliases);

    if (ALFloaterFriendsHere* friends_here = LLFloaterReg::findTypedInstance<ALFloaterFriendsHere>("friends_here"))
    {
        friends_here->refreshForAliasChange();
    }
    if (ALFloaterClubInvite* club_invite = LLFloaterReg::findTypedInstance<ALFloaterClubInvite>("club_invite"))
    {
        club_invite->refreshForAliasChange();
    }

    populateList();
}

void ALFloaterAliases::onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname)
{
    ALFloaterAliasesItem* item =
        mAliasList->getTypedItemByValue<ALFloaterAliasesItem>(LLSD(agent_id));
    if (item)
    {
        item->setAvatarName(avname.getDisplayName(), avname.getAccountName());
        if (mFilterEditor)
        {
            onFilterEdit(mFilterEditor->getText());
        }
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

void ALFloaterAliases::onFilterEdit(const std::string& search_string)
{
    std::vector<LLPanel*> items;
    mAliasList->getItems(items);

    S32 visible_count = 0;
    for (LLPanel* panel : items)
    {
        ALFloaterAliasesItem* item = dynamic_cast<ALFloaterAliasesItem*>(panel);
        if (!item)
        {
            continue;
        }

        bool visible = item->matchesFilter(search_string);
        item->setVisible(visible);
        if (visible)
        {
            visible_count++;
        }
    }

    mAliasList->notify(LLSD().with("rearrange", true));

    if (mStatusText)
    {
        if (search_string.empty())
        {
            mStatusText->setText(llformat("%d contacts", (S32)items.size()));
        }
        else
        {
            mStatusText->setText(llformat("%d contacts (filtered: %d)", (S32)items.size(), visible_count));
        }
    }
}
