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
#include <set>
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
    void refreshForAliasChange();
    void onClickHelloAll();

    bool hasBeenGreeted(const LLUUID& id) const { return mGreetedAvatars.count(id) > 0; }
    bool needsWelcomeBack(const LLUUID& id) const { return mNeedsWelcomeBack.count(id) > 0; }

    void onGreetSent(const LLUUID& id) { mGreetedAvatars.insert(id); }
    void onWelcomeBackSent(const LLUUID& id)
    {
        mNeedsWelcomeBack.erase(id);
        mGreetedAvatars.insert(id);
    }

private:
    bool tick() override;
    void refreshFriendsList();
    void onClickAliases();
    void onAvatarNameLoaded(const LLUUID& agent_id, const LLAvatarName& avname);
    void onCustomGreetingCommit();

    typedef std::map<LLUUID, F64> arrival_time_map_t;
    arrival_time_map_t mArrivalTimes;

    // Tracks when each avatar last left the agent's region. A return after a
    // real region leave resets the arrival time and re-flags welcome-back.
    arrival_time_map_t mAbsentSince;

    // Tracks when each greeted avatar stopped being seen in the agent's parcel,
    // so a transient absence does not falsely flag them as "left" and trigger
    // a welcome-back on their return.
    arrival_time_map_t mParcelAbsentSince;

    LLFlatListView* mFriendList = nullptr;
    LLTextBox*      mStatusText = nullptr;
    LLButton*       mRefreshBtn = nullptr;
    LLLineEditor*   mCustomGreetingEdit = nullptr;

    std::vector<boost::signals2::connection> mAvatarNameConnections;

    S32              mCurrentParcelID = -1;
    LLUUID           mCurrentRegionID;
    std::set<LLUUID> mGreetedAvatars;
    std::set<LLUUID> mLeftAfterGreeting;
    std::set<LLUUID> mNeedsWelcomeBack;
};

#endif // AL_FLOATER_FRIENDS_HERE_H
