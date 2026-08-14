/**
 * @file alfloateralias.h
 * @brief Central floater to manage the alias shared by the club invitation and
 *        friends here floaters.
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#ifndef AL_FLOATER_ALIAS_H
#define AL_FLOATER_ALIAS_H

#include "llfloater.h"
#include <boost/signals2.hpp>

#include <vector>

class LLCheckBoxCtrl;
class LLLineEditor;
class LLTextBox;
class LLFlatListView;
class LLButton;
class LLAvatarName;
class LLFilterEditor;

class ALFloaterAliases final : public LLFloater
{
public:
    ALFloaterAliases(const LLSD& key);
    ~ALFloaterAliases() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    void populateList();
    void onClickRefresh();
    void onClickSave();
    void onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname);
    void focusContact(const LLUUID& avatar_id);
    void onFilterEdit(const std::string& search_string);

    LLFlatListView* mAliasList = nullptr;
    LLButton*       mRefreshBtn = nullptr;
    LLButton*       mSaveBtn = nullptr;
    LLTextBox*      mStatusText = nullptr;
    LLFilterEditor* mFilterEditor = nullptr;

    std::vector<boost::signals2::connection> mAvatarNameConnections;
};

#endif // AL_FLOATER_ALIAS_H
