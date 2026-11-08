#include "FileCopyConflictTests.h"
#include "Blink/App/CopyConflictModel.h"
#include <iostream>
#include <initializer_list>

namespace
{
    bool Check(const bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }
}

bool RunFileCopyConflictTests()
{
    using namespace blink::app;
    using Policy = FileCopyConflictPolicy;
    bool passed = true;
    {
        CopyConflictModel model;
        passed &= Check(model.SelectedPolicy() == Policy::Cancel && model.Complete() == Policy::Cancel,
            "copy_conflict_dismissal_defaults_to_cancel_without_replacing_or_skipping");
    }
    {
        CopyConflictModel model(false);
        passed &= Check(!model.CanChoose(Policy::Replace) && !model.Choose(Policy::Replace) &&
            model.SelectedPolicy() == Policy::Cancel && model.Complete() == Policy::Cancel,
            "copy_conflict_unavailable_replace_cannot_be_submitted_programmatically");
    }
    {
        CopyConflictModel model(false);
        const auto denied = model.Choose(Policy::Replace);
        passed &= Check(!denied && model.Choose(Policy::KeepBoth) && model.Complete() == Policy::KeepBoth,
            "copy_conflict_disabled_replace_does_not_consume_valid_keep_both_choice");
    }
    {
        bool all_choices_preserved = true;
        for (const auto policy : { Policy::Skip, Policy::KeepBoth, Policy::Cancel, Policy::Replace })
        {
            CopyConflictModel model(true);
            all_choices_preserved &= model.CanChoose(policy) && model.Choose(policy) &&
                model.SelectedPolicy() == policy && model.Complete() == policy;
        }
        passed &= Check(all_choices_preserved,
            "copy_conflict_explicit_choices_deliver_the_requested_policy");
    }
    {
        CopyConflictModel model(true);
        const auto selected = model.Choose(Policy::KeepBoth);
        passed &= Check(selected && !model.Choose(Policy::Replace) && !model.Choose(Policy::Cancel) &&
            model.Complete() == Policy::KeepBoth,
            "copy_conflict_first_choice_survives_double_click_and_closing_event");
    }
    {
        CopyConflictModel model(true);
        const auto selected = model.Choose(Policy::Replace);
        const auto first = model.Complete();
        passed &= Check(selected && first == Policy::Replace && !model.Complete() &&
            model.IsClosed() && !model.Choose(Policy::Skip),
            "copy_conflict_completion_is_once_only_and_rejects_late_actions");
    }
    {
        CopyConflictModel model(true);
        const auto selected = model.Choose(Policy::Replace);
        model.Close();
        passed &= Check(selected && model.IsClosed() && !model.Complete() && !model.Choose(Policy::KeepBoth),
            "copy_conflict_owner_shutdown_suppresses_even_an_already_selected_replace");
    }
    {
        CopyConflictModel model(true);
        model.Close(); model.Close();
        passed &= Check(model.IsClosed() && !model.Choose(Policy::Cancel) && !model.Complete(),
            "copy_conflict_owner_close_is_idempotent_and_does_not_submit_cancel");
    }
    {
        CopyConflictModel model(true);
        passed &= Check(!model.Choose(Policy::Fail) && !model.Choose(static_cast<Policy>(999)) &&
            model.Complete() == Policy::Cancel,
            "copy_conflict_internal_or_unknown_policy_cannot_escape_as_user_consent");
    }
    return passed;
}
