#include <gtest/gtest.h>

#include <string>

#include <nexus/system/plan_apply.hpp>

using nexus::system::ApplyOutcome;
using nexus::system::layerWithRpmOstree;

// An outside review (0.1.84): names from repository metadata reached
// `rpm-ostree install` in a root shell as they were. A name that is not
// a package name now stops the call before anything runs.
TEST(PlanApplyTest, LayeringRefusesWhatIsNotAPackageName) {
    for (const char* hostile : {"langpacks-xx;id>/tmp/nexus-poc;#", "a b", "$(id)", "`id`", "x\ny", "--install=x"}) {
        const auto r = layerWithRpmOstree({"glibc-langpack-en", hostile});
        EXPECT_EQ(r.outcome, ApplyOutcome::Refused) << hostile;
        EXPECT_EQ(r.exitCode, 2) << hostile;
        ASSERT_FALSE(r.output.empty());
        EXPECT_NE(r.output[0].find("Not a package name"), std::string::npos);
    }
    EXPECT_EQ(layerWithRpmOstree({}).outcome, ApplyOutcome::Refused);
}
