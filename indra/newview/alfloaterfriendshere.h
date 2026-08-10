/**
 * @file alfloaterfriendshere.h
 * @brief Floater showing friends in the same place, sorted by how long they
 *        have been there, with a one-click nearby greeting.
 *
 * $LicenseInfo:2025&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2025, Alchemy Contributors
 * $/LicenseInfo$
 */

#ifndef AL_FLOATER_FRIENDS_HERE_H
#define AL_FLOATER_FRIENDS_HERE_H

#include "llfloater.h"
#include "lleventtimer.h"

#include <boost/signals2.hpp>

#include <map>
#include <vector>

class ALFriendsHereItem;
class LLFlatListView;
class LLTextBox;
class LLButton;
class LLLineEditor;
class LLAvatarName;
class LLFolderViewItem;

class ALFloaterFriendsHere final : public LLFloater, public LLEventTimer
{
public:
    ALFloaterFriendsHere(const LLSD& key);
    ~ALFloaterFriendsHere() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

private:
    bool tick() override;
    void refreshFriendsList();
    void onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname);
    void onCustomGreetingCommit();

    typedef std::map<LLUUID, F64> arrival_time_map_t;
    arrival_time_map_t mArrivalTimes;

    LLFlatListView* mFriendList = nullptr;
    LLTextBox*      mStatusText = nullptr;
    LLButton*       mRefreshBtn = nullptr;
    LLLineEditor*   mCustomGreetingEdit = nullptr;

    std::vector<boost::signals2::connection> mAvatarNameConnections;
};

#endif // AL_FLOATER_FRIENDS_HERE_H