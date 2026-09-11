/**
 * @file fspanelgreeter.cpp
 * @brief Greeter panel - nearby strangers not yet greeted
 */

#include "llviewerprecompiledheaders.h"

#include "fspanelgreeter.h"

#include "fsnearbychathub.h"
#include "llavatarlist.h"
#include "llavataractions.h"
#include "llcallingcard.h"
#include "llfiltereditor.h"
#include "lllineeditor.h"
#include "llbutton.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "lltrans.h"
#include "rlvactions.h"

static LLPanelInjector<FSPanelGreeter> t_greeter("fs_panel_greeter");

FSPanelGreeter::FSPanelGreeter()
:   LLPanel()
{
    mCommitCallbackRegistrar.add("Greeter.Greet", boost::bind(&FSPanelGreeter::onGreetClicked, this));
    mCommitCallbackRegistrar.add("Greeter.Reset", boost::bind(&FSPanelGreeter::onResetClicked, this));
}

FSPanelGreeter::~FSPanelGreeter()
{
    if (mRadarConn.connected()) mRadarConn.disconnect();
    if (mGreeterMessageConn.connected()) mGreeterMessageConn.disconnect();
}

bool FSPanelGreeter::postBuild()
{
    mGreeterList = getChild<LLAvatarList>("greeter_list");
    mGreeterList->setNoItemsCommentText(getString("no_newcomers"));
    mGreeterList->setNoItemsMsg(getString("no_newcomers"));
    mGreeterList->setNoFilteredItemsMsg(getString("no_filtered_newcomers"));
    mGreeterList->setShowIcons("NearbyListShowIcons");

    mFilterEditor = getChild<LLFilterEditor>("greeter_filter_input");
    mFilterEditor->setCommitCallback(boost::bind(&FSPanelGreeter::onFilterEdit, this, _2));

    mMessageEditor = getChild<LLLineEditor>("greeter_message_input");
    mMessageEditor->setCommitCallback(boost::bind(&FSPanelGreeter::onMessageChanged, this));
    mMessageEditor->setValue(gSavedSettings.getString("GreeterMessage"));

    mGreetButton = getChild<LLButton>("greet_btn");
    mResetButton = getChild<LLButton>("reset_btn");
    mCountText   = getChild<LLTextBox>("greeter_count");

    mRadarConn = FSRadar::getInstance()->setUpdateCallback(boost::bind(&FSPanelGreeter::updateList, this, _1, _2));

    // Refresh immediately with current data
    std::vector<LLSD> entries;
    LLSD stats;
    FSRadar::getInstance()->getCurrentData(entries, stats);
    updateList(entries, stats);

    updateButtons();
    return true;
}

void FSPanelGreeter::onFilterEdit(const std::string& search_string)
{
    std::string filter = search_string;
    LLStringUtil::trimHead(filter);
    mGreeterList->setNameFilter(filter);
}

void FSPanelGreeter::onMessageChanged()
{
    std::string val = mMessageEditor->getValue().asString();
    gSavedSettings.setString("GreeterMessage", val);
}

void FSPanelGreeter::updateList(const std::vector<LLSD>& entries, const LLSD& /*stats*/)
{
    mCurrentUngreeted.clear();
    mIdToName.clear();

    for (const auto& avdata : entries)
    {
        LLSD entry = avdata["entry"];
        LLUUID av_id = entry["id"].asUUID();
        std::string name = entry["name"].asString();

        if (av_id == gAgentID) continue;
        if (LLAvatarTracker::instance().isBuddy(av_id)) continue;
        if (mGreeted.find(av_id) != mGreeted.end()) continue;
        // RLVa: if names hidden, don't include? Still show but obey filter later.
        // Keep entry even if RLVa hides names - list will be empty if blocked.

        mCurrentUngreeted.push_back(av_id);
        mIdToName[av_id] = name;
    }

    rebuildList();
}

void FSPanelGreeter::rebuildList()
{
    if (!mGreeterList) return;

    uuid_vec_t& ids = mGreeterList->getIDs();
    ids = mCurrentUngreeted;
    mGreeterList->setDirty(true, ids.empty());

    // Update count text
    if (mCountText)
    {
        LLStringUtil::format_map_t args;
        args["[COUNT]"] = llformat("%d", (S32)mCurrentUngreeted.size());
        mCountText->setText(getString("greeter_count", args));
    }

    updateButtons();
}

void FSPanelGreeter::updateButtons()
{
    bool has_newcomers = !mCurrentUngreeted.empty();
    if (mGreetButton) mGreetButton->setEnabled(has_newcomers);
    // Reset enabled if any greeted exist
    if (mResetButton) mResetButton->setEnabled(!mGreeted.empty());
}

void FSPanelGreeter::onGreetClicked()
{
    if (mCurrentUngreeted.empty()) return;

    // Build comma-separated names in radar display order
    std::vector<std::string> names;
    names.reserve(mCurrentUngreeted.size());
    for (const LLUUID& id : mCurrentUngreeted)
    {
        auto it = mIdToName.find(id);
        if (it != mIdToName.end() && !it->second.empty())
            names.push_back(it->second);
        else
            names.push_back(id.asString()); // fallback
    }

    std::string joined;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i) joined += ", ";
        joined += names[i];
    }

    std::string tmpl = gSavedSettings.getString("GreeterMessage");
    std::string out;
    const std::string placeholder = "{NAMES}";
    auto pos = tmpl.find(placeholder);
    if (pos != std::string::npos)
    {
        out = tmpl;
        out.replace(pos, placeholder.size(), joined);
    }
    else if (!tmpl.empty())
    {
        // If user didn't include placeholder, append names
        // Avoid double spacing
        if (tmpl.back() != ' ' && tmpl.back() != ',' )
            out = tmpl + " " + joined;
        else
            out = tmpl + joined;
    }
    else
    {
        out = joined;
    }

    if (!out.empty())
    {
        FSNearbyChat::instance().sendChatFromViewer(out, CHAT_TYPE_NORMAL, false);
    }

    // Mark as greeted - sticky until Reset/relog
    for (const LLUUID& id : mCurrentUngreeted)
    {
        mGreeted.insert(id);
    }
    mCurrentUngreeted.clear();
    rebuildList();
}

void FSPanelGreeter::onResetClicked()
{
    mGreeted.clear();
    // Re-pull from radar to repopulate
    std::vector<LLSD> entries;
    LLSD stats;
    FSRadar::getInstance()->getCurrentData(entries, stats);
    updateList(entries, stats);
}
